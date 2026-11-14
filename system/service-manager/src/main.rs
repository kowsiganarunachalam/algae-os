use std::fs;
use std::io::{Read, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::os::unix::process::CommandExt;
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

const CONFIG_PATH: &str = "/etc/algae/services.conf";
const SOCKET_PATH: &str = "/run/algae-service-manager.sock";
const MAX_REQUEST: usize = 4096;
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

#[derive(Clone, Copy)]
enum State {
    Stopped,
    Running,
    Restarting,
    Failed,
}

struct Service {
    name: String,
    program: String,
    args: Vec<String>,
    child: Option<Child>,
    state: State,
    restart_at: Option<Instant>,
    restart_delay: Duration,
    auto_restart: bool,
}

impl Service {
    fn new(name: String, program: String, args: Vec<String>) -> Self {
        Self {
            name,
            program,
            args,
            child: None,
            state: State::Stopped,
            restart_at: None,
            restart_delay: Duration::from_secs(3),
            auto_restart: true,
        }
    }

    fn start(&mut self) -> String {
        if self.child.is_some() {
            return format!("{} is already running", self.name);
        }
        self.restart_at = None;
        self.auto_restart = true;
        let mut command = Command::new(&self.program);
        command.args(&self.args);
        // Give each service a process group so stop also reaps shell grandchildren.
        unsafe {
            command.pre_exec(|| {
                if setpgid(0, 0) == 0 {
                    Ok(())
                } else {
                    Err(std::io::Error::last_os_error())
                }
            });
        }
        match command.spawn() {
            Ok(child) => {
                let pid = child.id();
                self.child = Some(child);
                self.state = State::Running;
                format!("{} started (PID: {})", self.name, pid)
            }
            Err(error) => {
                self.state = State::Failed;
                self.restart_at = self
                    .auto_restart
                    .then(|| Instant::now() + self.restart_delay);
                format!("Failed to start {}: {}", self.name, error)
            }
        }
    }

    fn stop(&mut self) -> String {
        self.auto_restart = false;
        self.restart_at = None;
        if let Some(mut child) = self.child.take() {
            let pgid = child.id() as i32;
            unsafe {
                let _ = kill(-pgid, SIGTERM);
            }
            let deadline = Instant::now() + Duration::from_millis(500);
            loop {
                match child.try_wait() {
                    Ok(Some(_)) => break,
                    Ok(None) if Instant::now() < deadline => {
                        thread::sleep(Duration::from_millis(20))
                    }
                    _ => {
                        unsafe {
                            let _ = kill(-pgid, SIGKILL);
                        }
                        break;
                    }
                }
            }
            let _ = child.wait();
            // Ensure remaining members of the service process group are gone.
            unsafe {
                let _ = kill(-pgid, SIGKILL);
            }
            self.state = State::Stopped;
            format!("{} stopped", self.name)
        } else {
            self.state = State::Stopped;
            format!("{} is not running", self.name)
        }
    }

    fn restart(&mut self) -> String {
        let _ = self.stop();
        self.start()
    }

    fn schedule_restart(&mut self) {
        if self.auto_restart {
            self.restart_at = Some(Instant::now() + self.restart_delay);
            self.state = State::Restarting;
        } else {
            self.state = State::Failed;
        }
    }

    fn monitor(&mut self) {
        if let Some(mut child) = self.child.take() {
            match child.try_wait() {
                Ok(None) => self.child = Some(child),
                Ok(Some(status)) => {
                    eprintln!("[SERVICE] {} exited: {}", self.name, status);
                    self.schedule_restart();
                }
                Err(error) => {
                    eprintln!("[SERVICE] Failed to monitor {}: {}", self.name, error);
                    let pgid = child.id() as i32;
                    unsafe {
                        let _ = kill(-pgid, SIGKILL);
                    }
                    let _ = child.wait();
                    self.schedule_restart();
                }
            }
        }
        if self.child.is_none() && self.restart_at.is_some_and(|at| Instant::now() >= at) {
            let _ = self.start();
        }
    }

    fn status(&mut self) -> String {
        if let Some(child) = self.child.as_mut() {
            if matches!(child.try_wait(), Ok(None)) {
                return format!("{}: RUNNING (PID: {})", self.name, child.id());
            }
        }
        match self.state {
            State::Stopped => format!("{}: STOPPED", self.name),
            State::Running => format!("{}: FAILED", self.name),
            State::Restarting => format!("{}: RESTARTING", self.name),
            State::Failed => format!("{}: FAILED", self.name),
        }
    }
}

unsafe extern "C" {
    fn setpgid(pid: i32, pgid: i32) -> i32;
    fn kill(pid: i32, signal: i32) -> i32;
    fn signal(signal: i32, handler: extern "C" fn(i32)) -> usize;
}
const SIGTERM: i32 = 15;
const SIGKILL: i32 = 9;

extern "C" fn request_shutdown(_: i32) {
    SHUTDOWN.store(true, Ordering::Relaxed);
}

fn load_services() -> Result<Vec<Service>, String> {
    let contents = fs::read_to_string(CONFIG_PATH)
        .map_err(|e| format!("Cannot read {}: {}", CONFIG_PATH, e))?;
    let mut services = Vec::new();
    for (index, line) in contents.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<String> = line.split('|').map(|s| s.trim().to_owned()).collect();
        if fields.len() < 2 || fields[0].is_empty() || fields[1].is_empty() {
            eprintln!("[CONFIG] Invalid entry at line {}", index + 1);
            continue;
        }
        services.push(Service::new(
            fields[0].clone(),
            fields[1].clone(),
            fields[2..].to_vec(),
        ));
    }
    Ok(services)
}

fn read_request(stream: &mut UnixStream) -> std::io::Result<String> {
    let deadline = Instant::now() + Duration::from_secs(2);
    let mut request = Vec::with_capacity(64);
    let mut byte = [0u8; 1];
    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        if remaining.is_zero() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::TimedOut,
                "request timed out",
            ));
        }
        stream.set_read_timeout(Some(remaining))?;
        match stream.read(&mut byte)? {
            0 => break,
            _ if byte[0] == b'\n' => break,
            _ => {
                if request.len() == MAX_REQUEST {
                    return Err(std::io::Error::new(
                        std::io::ErrorKind::InvalidData,
                        "request too large",
                    ));
                }
                request.push(byte[0]);
            }
        }
    }
    String::from_utf8(request).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
}

fn handle_client(mut stream: UnixStream, services: &mut [Service]) {
    let response = match read_request(&mut stream) {
        Ok(request) => {
            let parts: Vec<&str> = request.split_whitespace().collect();
            match parts.as_slice() {
                ["status"] => {
                    let mut output = String::from("Algae Service Manager: RUNNING\n");
                    for service in services {
                        output.push_str(&service.status());
                        output.push('\n');
                    }
                    output
                }
                [action @ ("start" | "stop" | "restart"), name] => {
                    match services.iter_mut().find(|s| s.name == *name) {
                        Some(service) => match *action {
                            "start" => service.start(),
                            "stop" => service.stop(),
                            "restart" => service.restart(),
                            _ => unreachable!(),
                        },
                        None => format!("Unknown service: {}", name),
                    }
                }
                _ => {
                    "Invalid command. Use status, start NAME, stop NAME or restart NAME.".to_owned()
                }
            }
        }
        Err(error) => format!("Request error: {}", error),
    };
    let _ = stream.write_all(response.as_bytes());
    if !response.ends_with('\n') {
        let _ = stream.write_all(b"\n");
    }
    let _ = stream.shutdown(std::net::Shutdown::Write);
}

fn main() {
    let mut services = match load_services() {
        Ok(s) => s,
        Err(e) => {
            eprintln!("[FATAL] {}", e);
            return;
        }
    };
    let listener = match UnixListener::bind(SOCKET_PATH) {
        Ok(l) => l,
        Err(first_error) if first_error.kind() == std::io::ErrorKind::AddrInUse => {
            if UnixStream::connect(SOCKET_PATH).is_ok() {
                eprintln!(
                    "[FATAL] Another service manager is listening on {}",
                    SOCKET_PATH
                );
                return;
            }
            if let Err(e) = fs::remove_file(SOCKET_PATH) {
                eprintln!("[FATAL] Cannot remove stale IPC socket: {}", e);
                return;
            }
            match UnixListener::bind(SOCKET_PATH) {
                Ok(l) => l,
                Err(e) => {
                    eprintln!("[FATAL] Cannot bind {}: {}", SOCKET_PATH, e);
                    return;
                }
            }
        }
        Err(e) => {
            eprintln!("[FATAL] Cannot bind {}: {}", SOCKET_PATH, e);
            return;
        }
    };
    unsafe {
        signal(SIGTERM, request_shutdown);
        signal(2, request_shutdown);
    }
    if let Err(e) = fs::set_permissions(SOCKET_PATH, PermissionsExt::from_mode(0o660)) {
        eprintln!("[IPC] Cannot set socket permissions: {}", e);
    }
    if let Err(e) = listener.set_nonblocking(true) {
        eprintln!("[FATAL] Cannot configure IPC socket: {}", e);
        return;
    }
    for service in &mut services {
        eprintln!("[SERVICE] {}", service.start());
    }
    eprintln!("[IPC] Listening on {}", SOCKET_PATH);
    while !SHUTDOWN.load(Ordering::Relaxed) {
        match listener.accept() {
            Ok((stream, _)) => handle_client(stream, &mut services),
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(e) => eprintln!("[IPC] {}", e),
        }
        for service in &mut services {
            service.monitor();
        }
        thread::sleep(Duration::from_millis(50));
    }
    for service in &mut services {
        let _ = service.stop();
    }
    let _ = fs::remove_file(SOCKET_PATH);
}
