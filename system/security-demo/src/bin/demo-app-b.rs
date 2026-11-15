use std::fs;
use std::io::ErrorKind;
use std::net::UdpSocket;
use std::time::Duration;

fn main() {
    let own = fs::read_to_string("/usr/share/demo-app-b/secret.txt")
        .expect("App B must read its own resource");
    println!("App B own resource: {}", own.trim());

    let other = fs::read_to_string("/usr/share/demo-app-a/secret.txt");
    assert!(
        other.is_err(),
        "Landlock should deny App B access to App A resources"
    );
    println!("App B cross-app read: denied");

    let private = "/var/lib/algae/apps/demo-app-b/private.txt";
    fs::write(private, "B private data").expect("App B must write its private directory");
    println!("App B private write: allowed");

    match fs::read_to_string("/var/lib/algae/apps/demo-app-a/private.txt") {
        Err(error) if error.kind() == ErrorKind::PermissionDenied => {
            println!("App B App A private read: denied");
        }
        Err(error) if error.kind() == ErrorKind::NotFound => {
            println!("App B App A private read: skipped (App A has not run yet)");
        }
        Err(error) => panic!("unexpected private-file error: {error}"),
        Ok(_) => panic!("App B unexpectedly read App A private data"),
    }

    let socket = UdpSocket::bind("0.0.0.0:0").expect("App B outbound socket should be allowed");
    socket
        .set_read_timeout(Some(Duration::from_secs(3)))
        .unwrap();
    let query = [
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, b'e', b'x',
        b'a', b'm', b'p', b'l', b'e', 0x03, b'c', b'o', b'm', 0x00, 0x00, 0x01, 0x00, 0x01,
    ];
    socket
        .send_to(&query, "10.0.2.3:53")
        .expect("App B DNS request should pass nftables");
    let mut response = [0u8; 512];
    let (length, _) = socket
        .recv_from(&mut response)
        .expect("App B should receive DNS response");
    assert!(
        length >= 12 && response[0..2] == [0x12, 0x34],
        "invalid DNS response"
    );
    println!("App B outbound DNS: allowed");
}
