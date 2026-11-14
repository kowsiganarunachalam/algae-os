
use std::process::{Child, Command};
use std::thread;
use std::time::Duration;

struct Service {
    name: String,
    program: String,
    args: Vec<String>,
    child: Option<Child>,
    restart_delay: Duration,
}

impl Service {
    fn new(name: &str, program: &str, args: &[&str]) -> Self {
        Self {
            name: name.to_string(),
            program: program.to_string(),
            args: args.iter().map(|s| s.to_string()).collect(),
            child: None,
            restart_delay: Duration::from_secs(3),
        }
    }

    fn start(&mut self) {
        println!("[SERVICE] Starting {}", self.name);

        match Command::new(&self.program)
            .args(&self.args)
            .spawn()
        {
            Ok(child) => {
                println!(
                    "[SERVICE] {} started (PID: {})",
                    self.name,
                    child.id()
                );

                self.child = Some(child);
            }

            Err(error) => {
                eprintln!(
                    "[ERROR] Failed to start {}: {}",
                    self.name,
                    error
                );
            }
        }
    }

    fn monitor(&mut self) {
        let Some(child) = self.child.as_mut() else {
            thread::sleep(self.restart_delay);
            self.start();
            return;
        };

        match child.try_wait() {
            Ok(Some(status)) => {
                println!(
                    "[SERVICE] {} exited: {}",
                    self.name, status
                );

                self.child = None;

                println!(
                    "[SERVICE] Restarting {} in {:?}",
                    self.name, self.restart_delay
                );

                thread::sleep(self.restart_delay);
                self.start();
            }

            Ok(None) => {
                // Service is still running.
            }

            Err(error) => {
                eprintln!(
                    "[ERROR] Monitoring {}: {}",
                    self.name, error
                );

                self.child = None;
                thread::sleep(self.restart_delay);
                self.start();
            }
        }
    }
}

fn main() {
    println!("================================");
    println!("     Algae Service Manager");
    println!("================================");

    let mut demo = Service::new(
        "demo-service",
        "/bin/sh",
        &[
            "-c",
            "while true; do echo '[demo-service] Running'; sleep 5; done",
        ],
    );

    demo.start();

    loop {
        demo.monitor();
        thread::sleep(Duration::from_secs(1));
    }
}

