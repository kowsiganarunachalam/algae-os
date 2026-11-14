use std::os::unix::process::CommandExt;
use std::process::Command;
use std::thread;
use std::time::Duration;

fn main() {
    println!("================================");
    println!("          Algae OS Init");
    println!("================================");
    println!("Architecture: ARM64");
    println!("Process ID: {}", std::process::id());
    println!("Status: Starting system initialization...");
    println!("Starting BusyBox init...");

    let error = Command::new("/bin/busybox")
        .arg("init")
        .exec();

    eprintln!("Algae Init: Failed to start BusyBox init: {}", error);

    // PID 1 must not simply exit.
    loop {
        thread::sleep(Duration::from_secs(60));
    }
}
