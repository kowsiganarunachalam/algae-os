use std::env;
use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::Duration;

const SOCKET_PATH: &str = "/run/algae-service-manager.sock";

fn usage() {
    eprintln!("Usage: algae-service <status|start NAME|stop NAME|restart NAME>");
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if matches!(args.as_slice(), [command] if command == "help" || command == "--help" || command == "-h")
    {
        usage();
        return;
    }
    let valid = matches!(args.as_slice(), [action] if action == "status")
        || matches!(args.as_slice(), [action, name] if ["start", "stop", "restart"].contains(&action.as_str()) && !name.is_empty() && !name.chars().any(char::is_whitespace));
    if !valid {
        usage();
        std::process::exit(2);
    }

    let mut stream = match UnixStream::connect(SOCKET_PATH) {
        Ok(stream) => stream,
        Err(error) => {
            eprintln!(
                "Cannot connect to service manager at {}: {}",
                SOCKET_PATH, error
            );
            std::process::exit(1);
        }
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(3)));
    if writeln!(stream, "{}", args.join(" ")).is_err() {
        eprintln!("Failed to send request to service manager");
        std::process::exit(1);
    }
    let mut response = Vec::new();
    if let Err(error) = stream.take(64 * 1024).read_to_end(&mut response) {
        eprintln!("Failed to read response from service manager: {}", error);
        std::process::exit(1);
    }
    print!("{}", String::from_utf8_lossy(&response));
}
