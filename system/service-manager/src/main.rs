use std::ffi::CString;
use std::fs;
use std::io::{Read, Write};
use std::os::unix::ffi::OsStrExt;
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::os::unix::net::{UnixListener, UnixStream};
use std::os::unix::process::CommandExt;
use std::path::{Component, Path, PathBuf};
use std::process::{Child, Command};
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::{Duration, Instant};

const CONFIG_PATH: &str = "/etc/algae/services.conf";
const SOCKET_PATH: &str = "/run/algae-service-manager.sock";
const MAX_REQUEST: usize = 4096;
const APP_POLICY_DIR: &str = "/etc/algae/permissions";
const EINVAL: i32 = 22;
static SHUTDOWN: AtomicBool = AtomicBool::new(false);

#[derive(Default)]
struct AppPolicy {
    name: String,
    user: String,
    read_paths: Vec<PathBuf>,
    write_paths: Vec<PathBuf>,
    outbound: bool,
    inbound: bool,
    namespaces: bool,
    seccomp: bool,
    landlock: bool,
}

impl AppPolicy {
    fn load(name: &str) -> Result<Self, String> {
        if name.is_empty()
            || !name
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
        {
            return Err("invalid application name".into());
        }
        let path = format!("{}/{}.conf", APP_POLICY_DIR, name);
        let metadata =
            fs::symlink_metadata(&path).map_err(|e| format!("cannot inspect {}: {}", path, e))?;
        if !metadata.file_type().is_file()
            || metadata.uid() != 0
            || metadata.permissions().mode() & 0o022 != 0
        {
            return Err(format!(
                "{} must be a root-owned, non-writable policy file",
                path
            ));
        }
        let contents =
            fs::read_to_string(&path).map_err(|e| format!("cannot read {}: {}", path, e))?;
        let mut policy = Self::default();
        let mut section = String::new();
        let mut seen = std::collections::HashSet::new();
        for (index, raw) in contents.lines().enumerate() {
            let line = raw.trim();
            if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
                continue;
            }
            if line.starts_with('[') && line.ends_with(']') {
                section = line[1..line.len() - 1].to_owned();
                if !matches!(
                    section.as_str(),
                    "application" | "filesystem" | "network" | "security"
                ) {
                    return Err(format!("unknown section at {}:{}", path, index + 1));
                }
                continue;
            }
            let (key, value) = line
                .split_once('=')
                .ok_or_else(|| format!("invalid entry at {}:{}", path, index + 1))?;
            let key = key.trim();
            let value = value.trim();
            let field = format!("{}.{}", section, key);
            let repeated = matches!(field.as_str(), "filesystem.read" | "filesystem.write");
            if section.is_empty() || (!repeated && !seen.insert(field.clone())) {
                return Err(format!(
                    "duplicate or out-of-section key at {}:{}",
                    path,
                    index + 1
                ));
            }
            match field.as_str() {
                "application.name" => policy.name = value.to_owned(),
                "application.user" => policy.user = value.to_owned(),
                "filesystem.read" => policy
                    .read_paths
                    .push(validate_policy_path(value, name, false)?),
                "filesystem.write" => policy
                    .write_paths
                    .push(validate_policy_path(value, name, true)?),
                "network.outbound" => policy.outbound = parse_bool(value, &field)?,
                "network.inbound" => policy.inbound = parse_bool(value, &field)?,
                "security.namespaces" => policy.namespaces = parse_bool(value, &field)?,
                "security.seccomp" => policy.seccomp = parse_bool(value, &field)?,
                "security.landlock" => policy.landlock = parse_bool(value, &field)?,
                _ => return Err(format!("unknown permission key at {}:{}", path, index + 1)),
            }
        }
        for required in [
            "application.name",
            "application.user",
            "network.outbound",
            "network.inbound",
            "security.namespaces",
            "security.seccomp",
            "security.landlock",
        ] {
            if !seen.contains(required) {
                return Err(format!("missing {} in {}", required, path));
            }
        }
        if policy.name != name {
            return Err(format!("policy name does not match {}", name));
        }
        if policy.user.is_empty()
            || !policy
                .user
                .bytes()
                .all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-' || b == b'_')
        {
            return Err("invalid application user".into());
        }
        if policy.read_paths.is_empty() || policy.write_paths.is_empty() {
            return Err("at least one read and one write path are required".into());
        }
        if policy.namespaces {
            return Err("per-application namespaces are not implemented".into());
        }
        if policy.inbound {
            return Err("per-application inbound network grants are not implemented".into());
        }
        Ok(policy)
    }
}

fn resolve_app_identity(user: &str) -> Result<(u32, u32), String> {
    let passwd =
        fs::read_to_string("/etc/passwd").map_err(|e| format!("cannot read /etc/passwd: {}", e))?;
    for line in passwd.lines() {
        let fields: Vec<&str> = line.split(':').collect();
        if fields.len() == 7 && fields[0] == user {
            let uid = fields[2].parse().map_err(|_| "invalid app UID in passwd")?;
            let gid = fields[3].parse().map_err(|_| "invalid app GID in passwd")?;
            if uid < 1000 || gid < 1000 {
                return Err("application identities must be unprivileged".into());
            }
            return Ok((uid, gid));
        }
    }
    Err(format!("application user {} does not exist", user))
}

fn run_app(args: &[String]) -> Result<i32, String> {
    if unsafe { geteuid() } != 0 {
        return Err("--run-app must be invoked by the privileged service manager".into());
    }
    if args.len() < 4 || args[2] != "--" || !Path::new(&args[3]).is_absolute() {
        return Err("usage: algae-service-manager --run-app NAME -- PROGRAM [ARG ...]".into());
    }
    let policy = AppPolicy::load(&args[1])?;
    let (uid, gid) = resolve_app_identity(&policy.user)?;
    let read_paths = trusted_read_paths(&policy)?;
    let write_paths = policy.write_paths.clone();
    let use_landlock = policy.landlock;
    let use_seccomp = policy.seccomp;
    let network_allowed = policy.outbound;
    let mut command = Command::new(&args[3]);
    command.args(&args[4..]);
    unsafe {
        command.pre_exec(move || {
            if setgroups(0, std::ptr::null()) != 0
                || drop_capability_bounding_set() != 0
                || setgid(gid) != 0
                || setuid(uid) != 0
                || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0
            {
                return Err(std::io::Error::last_os_error());
            }
            if use_landlock {
                apply_landlock(&read_paths, &write_paths)?;
            }
            if use_seccomp && !network_allowed {
                install_network_seccomp()?;
            }
            Ok(())
        });
    }
    let nft_grant = if policy.outbound {
        Some(install_outbound_grant(&policy.name, uid)?)
    } else {
        None
    };
    eprintln!(
        "[SECURITY] launching app={} uid={} landlock={} seccomp={}",
        policy.name, uid, policy.landlock, policy.seccomp
    );
    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(error) => {
            if let Some(handle) = nft_grant {
                let _ = remove_outbound_grant(handle);
            }
            return Err(format!("failed to launch {}: {}", policy.name, error));
        }
    };
    let wait_result = child.wait();
    if let Some(handle) = nft_grant {
        remove_outbound_grant(handle)?;
    }
    let status = wait_result.map_err(|e| format!("failed waiting for {}: {}", policy.name, e))?;
    eprintln!(
        "[SECURITY] app={} pid={} exited status={}",
        policy.name,
        child.id(),
        status
    );
    Ok(status.code().unwrap_or(128))
}

fn install_outbound_grant(name: &str, uid: u32) -> Result<u32, String> {
    let output = Command::new("/usr/sbin/nft")
        .args(["add", "element", "inet", "algae", "app_outbound", "{"])
        .arg(uid.to_string())
        .arg("}")
        .output()
        .map_err(|e| format!("cannot run nft for app network grant: {}", e))?;
    if output.status.success() {
        Ok(uid)
    } else {
        Err(format!(
            "nft refused outbound grant for {}: {}",
            name,
            String::from_utf8_lossy(&output.stderr).trim()
        ))
    }
}

fn remove_outbound_grant(uid: u32) -> Result<(), String> {
    let output = Command::new("/usr/sbin/nft")
        .args(["delete", "element", "inet", "algae", "app_outbound", "{"])
        .arg(uid.to_string())
        .arg("}")
        .output()
        .map_err(|e| format!("cannot remove temporary nft grant: {}", e))?;
    if output.status.success() {
        Ok(())
    } else {
        Err(format!(
            "cannot remove temporary nft grant: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ))
    }
}

fn trusted_read_paths(policy: &AppPolicy) -> Result<Vec<PathBuf>, String> {
    let mut paths = policy.read_paths.clone();
    for system_path in ["/usr/bin", "/usr/lib", "/lib"] {
        let p = Path::new(system_path);
        if p.exists() {
            let canonical = p
                .canonicalize()
                .map_err(|e| format!("cannot resolve runtime path {}: {}", system_path, e))?;
            if !paths.contains(&canonical) {
                paths.push(canonical);
            }
        }
    }
    Ok(paths)
}

unsafe fn apply_landlock(read_paths: &[PathBuf], write_paths: &[PathBuf]) -> std::io::Result<()> {
    const CREATE_RULESET: i64 = 444;
    const ADD_RULE: i64 = 445;
    const RESTRICT_SELF: i64 = 446;
    const VERSION_FLAG: u32 = 1;
    const RULE_PATH_BENEATH: u32 = 1;
    const FS_EXECUTE: u64 = 1 << 0;
    const FS_WRITE_FILE: u64 = 1 << 1;
    const FS_READ_FILE: u64 = 1 << 2;
    const FS_READ_DIR: u64 = 1 << 3;
    const FS_WRITE_RIGHTS: u64 = ((1 << 16) - 1) & !(FS_EXECUTE | FS_READ_FILE | FS_READ_DIR);
    const FS_ALL: u64 = (1 << 16) - 1;
    const O_PATH_CLOEXEC: i32 = 0x200000 | 0x80000;

    let abi = unsafe { syscall(CREATE_RULESET, std::ptr::null::<u8>(), 0usize, VERSION_FLAG) };
    if abi < 1 {
        return Err(std::io::Error::last_os_error());
    }
    let ruleset_attr = LandlockRulesetAttr {
        handled_access_fs: FS_ALL,
        handled_access_net: 0,
        scoped: 0,
    };
    let ruleset = unsafe {
        syscall(
            CREATE_RULESET,
            &ruleset_attr,
            std::mem::size_of::<LandlockRulesetAttr>(),
            0u32,
        )
    };
    if ruleset < 0 {
        return Err(std::io::Error::last_os_error());
    }
    let add_path = |path: &Path, allowed_access: u64| -> std::io::Result<()> {
        let c_path = CString::new(path.as_os_str().as_bytes())
            .map_err(|_| std::io::Error::from_raw_os_error(EINVAL))?;
        let fd = unsafe { libc_open(c_path.as_ptr(), O_PATH_CLOEXEC) };
        if fd < 0 {
            return Err(std::io::Error::last_os_error());
        }
        let attr = LandlockPathBeneathAttr {
            allowed_access,
            parent_fd: fd,
            reserved: 0,
        };
        let result = unsafe { syscall(ADD_RULE, ruleset as i32, RULE_PATH_BENEATH, &attr, 0u32) };
        unsafe {
            close(fd);
        }
        if result < 0 {
            Err(std::io::Error::last_os_error())
        } else {
            Ok(())
        }
    };
    let read_access = FS_EXECUTE | FS_READ_FILE | FS_READ_DIR;
    for path in read_paths {
        if let Err(error) = add_path(path, read_access) {
            unsafe {
                close(ruleset as i32);
            }
            return Err(error);
        }
    }
    for path in write_paths {
        if let Err(error) = add_path(path, read_access | FS_WRITE_FILE | FS_WRITE_RIGHTS) {
            unsafe {
                close(ruleset as i32);
            }
            return Err(error);
        }
    }
    let result = unsafe { syscall(RESTRICT_SELF, ruleset as i32, 0u32) };
    unsafe {
        close(ruleset as i32);
    }
    if result < 0 {
        Err(std::io::Error::last_os_error())
    } else {
        Ok(())
    }
}

unsafe fn install_network_seccomp() -> std::io::Result<()> {
    const PR_SET_SECCOMP: i32 = 22;
    const SECCOMP_MODE_FILTER: usize = 2;
    const RET_KILL_PROCESS: u32 = 0x80000000;
    const RET_ERRNO: u32 = 0x00050000;
    const RET_ALLOW: u32 = 0x7fff0000;
    const AUDIT_ARCH_AARCH64: u32 = 0xc00000b7;
    // AArch64 Linux syscall numbers for socket APIs, accept4, and io_uring.
    let denied = [
        198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 242, 243, 269,
        425,
    ];
    let mut filter = vec![
        SockFilter {
            code: 0x20,
            jt: 0,
            jf: 0,
            k: 4,
        },
        SockFilter {
            code: 0x15,
            jt: 1,
            jf: 0,
            k: AUDIT_ARCH_AARCH64,
        },
        SockFilter {
            code: 0x06,
            jt: 0,
            jf: 0,
            k: RET_KILL_PROCESS,
        },
        SockFilter {
            code: 0x20,
            jt: 0,
            jf: 0,
            k: 0,
        },
    ];
    for number in denied {
        filter.push(SockFilter {
            code: 0x15,
            jt: 0,
            jf: 1,
            k: number,
        });
        filter.push(SockFilter {
            code: 0x06,
            jt: 0,
            jf: 0,
            k: RET_ERRNO | 1,
        });
    }
    filter.push(SockFilter {
        code: 0x06,
        jt: 0,
        jf: 0,
        k: RET_ALLOW,
    });
    let program = SockFprog {
        len: filter.len() as u16,
        filter: filter.as_ptr(),
    };
    if unsafe {
        prctl(
            PR_SET_SECCOMP,
            SECCOMP_MODE_FILTER,
            &program as *const _ as usize,
            0,
            0,
        )
    } != 0
    {
        return Err(std::io::Error::last_os_error());
    }
    Ok(())
}

fn parse_bool(value: &str, field: &str) -> Result<bool, String> {
    match value {
        "true" => Ok(true),
        "false" => Ok(false),
        _ => Err(format!("{} must be true or false", field)),
    }
}

fn validate_policy_path(value: &str, app: &str, write: bool) -> Result<PathBuf, String> {
    let path = Path::new(value);
    if !path.is_absolute()
        || path
            .components()
            .any(|c| matches!(c, Component::ParentDir | Component::CurDir))
        || value == "/"
        || value.starts_with("/proc")
        || value.starts_with("/sys")
        || value.starts_with("/dev")
        || value.starts_with("/etc")
        || value.starts_with("/run")
    {
        return Err(format!("unsafe filesystem path: {}", value));
    }
    let expected = if write {
        PathBuf::from(format!("/var/lib/algae/apps/{}", app))
    } else {
        PathBuf::from(format!("/usr/share/{}", app))
    };
    if path != expected {
        return Err(format!(
            "{} path must be {}",
            if write { "write" } else { "read" },
            expected.display()
        ));
    }
    let canonical = path
        .canonicalize()
        .map_err(|e| format!("cannot validate {}: {}", value, e))?;
    if canonical != path {
        return Err(format!("symlink paths are not permitted: {}", value));
    }
    Ok(canonical)
}

#[repr(C)]
struct LandlockRulesetAttr {
    handled_access_fs: u64,
    handled_access_net: u64,
    scoped: u64,
}

#[repr(C)]
struct LandlockPathBeneathAttr {
    allowed_access: u64,
    parent_fd: i32,
    reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct SockFilter {
    code: u16,
    jt: u8,
    jf: u8,
    k: u32,
}

#[repr(C)]
struct SockFprog {
    len: u16,
    filter: *const SockFilter,
}

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
    uid: u32,
    gid: u32,
    args: Vec<String>,
    child: Option<Child>,
    state: State,
    restart_at: Option<Instant>,
    restart_delay: Duration,
    auto_restart: bool,
}

impl Service {
    fn new(name: String, program: String, uid: u32, gid: u32, args: Vec<String>) -> Self {
        Self {
            name,
            program,
            uid,
            gid,
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
        // Apply identity and privilege restrictions in the child before exec.
        // Failure in any step aborts spawn; the service is never run with a
        // partially applied policy.
        let (uid, gid) = (self.uid, self.gid);
        unsafe {
            command.pre_exec(move || {
                if setpgid(0, 0) != 0
                    || setgroups(0, std::ptr::null()) != 0
                    || (uid != 0 && drop_capability_bounding_set() != 0)
                    || setgid(gid) != 0
                    || setuid(uid) != 0
                    || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0
                {
                    return Err(std::io::Error::last_os_error());
                }
                Ok(())
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

unsafe fn drop_capability_bounding_set() -> i32 {
    const PR_CAPBSET_READ: i32 = 23;
    const PR_CAPBSET_DROP: i32 = 24;
    const CAP_SETPCAP: usize = 8;

    // CAP_SETPCAP is needed to remove the other bounding-set entries, so drop
    // it last. Read each capability first and stop at EINVAL so the kernel's
    // actual CAP_LAST_CAP, rather than a build-time guess, bounds the loop.
    for cap in 0..=255usize {
        if cap == CAP_SETPCAP {
            continue;
        }
        let present = unsafe { prctl(PR_CAPBSET_READ, cap, 0, 0, 0) };
        if present < 0 {
            if std::io::Error::last_os_error().raw_os_error() == Some(EINVAL) {
                break;
            }
            return -1;
        }
        if present == 1 && unsafe { prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) } != 0 {
            return -1;
        }
    }
    let present = unsafe { prctl(PR_CAPBSET_READ, CAP_SETPCAP, 0, 0, 0) };
    if present < 0 {
        return -1;
    }
    if present == 1 && unsafe { prctl(PR_CAPBSET_DROP, CAP_SETPCAP, 0, 0, 0) } != 0 {
        return -1;
    }
    0
}

unsafe extern "C" {
    fn geteuid() -> u32;
    fn setpgid(pid: i32, pgid: i32) -> i32;
    fn setgroups(size: usize, list: *const u32) -> i32;
    fn setgid(gid: u32) -> i32;
    fn setuid(uid: u32) -> i32;
    fn prctl(option: i32, arg2: usize, arg3: usize, arg4: usize, arg5: usize) -> i32;
    fn kill(pid: i32, signal: i32) -> i32;
    fn signal(signal: i32, handler: extern "C" fn(i32)) -> usize;
    #[link_name = "open"]
    fn libc_open(path: *const std::os::raw::c_char, flags: i32, ...) -> i32;
    fn close(fd: i32) -> i32;
    fn syscall(number: i64, ...) -> i64;
}
const SIGTERM: i32 = 15;
const SIGKILL: i32 = 9;
const PR_SET_NO_NEW_PRIVS: i32 = 38;

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
        if fields.len() < 4 || fields[0].is_empty() || fields[1].is_empty() {
            return Err(format!("Invalid service entry at line {}", index + 1));
        }
        let uid = fields[2]
            .parse::<u32>()
            .map_err(|_| format!("Invalid UID at services.conf line {}", index + 1))?;
        let gid = fields[3]
            .parse::<u32>()
            .map_err(|_| format!("Invalid GID at services.conf line {}", index + 1))?;
        if services
            .iter()
            .any(|service: &Service| service.name == fields[0])
        {
            return Err(format!("Duplicate service name at line {}", index + 1));
        }
        if (uid == 0) != (gid == 0) || (uid == 0 && fields[0] != "weston") {
            return Err(format!("Invalid privileged identity at line {}", index + 1));
        }
        services.push(Service::new(
            fields[0].clone(),
            fields[1].clone(),
            uid,
            gid,
            fields[4..].to_vec(),
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
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.first().is_some_and(|arg| arg == "--run-app") {
        match run_app(&args) {
            Ok(code) => std::process::exit(code),
            Err(error) => {
                eprintln!("[SECURITY] app launch denied: {}", error);
                std::process::exit(126);
            }
        }
    }
    if unsafe { geteuid() } != 0 {
        eprintln!("[FATAL] Service manager must run as root to apply service identities");
        return;
    }
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
