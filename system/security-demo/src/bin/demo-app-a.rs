use std::fs;
use std::io::ErrorKind;
use std::net::UdpSocket;
use std::path::Path;

fn main() {
    let own = fs::read_to_string("/usr/share/demo-app-a/secret.txt")
        .expect("App A must read its own resource");
    println!("App A own resource: {}", own.trim());

    let other = fs::read_to_string("/usr/share/demo-app-b/secret.txt");
    assert!(
        other.is_err(),
        "Landlock should deny App A access to App B resources"
    );
    println!("App A cross-app read: denied");

    let private = Path::new("/var/lib/algae/apps/demo-app-a/private.txt");
    fs::write(private, "A private data").expect("App A must write its private directory");
    println!("App A private write: allowed");

    match fs::read_to_string("/var/lib/algae/apps/demo-app-b/private.txt") {
        Err(error) if error.kind() == ErrorKind::PermissionDenied => {
            println!("App A App B private read: denied");
        }
        Err(error) if error.kind() == ErrorKind::NotFound => {
            println!("App A App B private read: skipped (App B has not run yet)");
        }
        Err(error) => panic!("unexpected private-file error: {error}"),
        Ok(_) => panic!("App A unexpectedly read App B private data"),
    }

    match UdpSocket::bind("0.0.0.0:0") {
        Err(error) if error.raw_os_error() == Some(1) => {
            println!("App A socket creation: denied by seccomp");
        }
        Err(error) => panic!("unexpected socket error: {error}"),
        Ok(_) => panic!("App A unexpectedly created a network socket"),
    }
}
