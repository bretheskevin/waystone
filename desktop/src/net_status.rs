use std::net::{TcpStream, ToSocketAddrs};
use std::time::Duration;

const PROBE_TIMEOUT: Duration = Duration::from_secs(4);
const INTERNET_PROBE: (&str, u16) = ("1.1.1.1", 443);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NoInternetReason {
    NoNetwork,
    ServerUnreachable,
}

impl NoInternetReason {
    pub fn headline(self) -> &'static str {
        match self {
            Self::NoNetwork => "No internet connection detected.",
            Self::ServerUnreachable => "Can't reach your Waystone server.",
        }
    }

    pub fn detail(self) -> &'static str {
        match self {
            Self::NoNetwork => "Check your Wi-Fi settings and try again.",
            Self::ServerUnreachable => "Check the server URL in Settings and try again.",
        }
    }
}

pub const RETRY_SUFFIX: &str = "Still no connection. Try again.";

pub fn classify(server_ok: bool, internet_ok: bool) -> Option<NoInternetReason> {
    if server_ok {
        None
    } else if internet_ok {
        Some(NoInternetReason::ServerUnreachable)
    } else {
        Some(NoInternetReason::NoNetwork)
    }
}

pub fn host_port(server_url: &str) -> Option<(String, u16)> {
    let url = reqwest::Url::parse(server_url).ok()?;
    Some((url.host_str()?.to_string(), url.port_or_known_default()?))
}

fn reachable(host: &str, port: u16) -> bool {
    let Ok(addrs) = (host, port).to_socket_addrs() else {
        return false;
    };
    addrs
        .into_iter()
        .any(|a| TcpStream::connect_timeout(&a, PROBE_TIMEOUT).is_ok())
}

/// Blocking: call from spawn_blocking / non-async code.
pub fn probe(server_url: &str) -> Option<NoInternetReason> {
    let t0 = std::time::Instant::now();
    let server_ok = host_port(server_url).is_some_and(|(h, p)| reachable(&h, p));
    let internet_ok = server_ok || reachable(INTERNET_PROBE.0, INTERNET_PROBE.1);
    let r = classify(server_ok, internet_ok);
    log::info!(
        "[net] probe {}: server_ok={server_ok} internet_ok={internet_ok} -> {r:?} in {} ms",
        waystone_core::url::redact_userinfo(server_url),
        t0.elapsed().as_millis()
    );
    r
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classify_truth_table() {
        assert_eq!(classify(true, true), None);
        assert_eq!(classify(true, false), None);
        assert_eq!(
            classify(false, true),
            Some(NoInternetReason::ServerUnreachable)
        );
        assert_eq!(classify(false, false), Some(NoInternetReason::NoNetwork));
    }

    #[test]
    fn host_port_parsing() {
        assert_eq!(
            host_port("https://x.sslip.io"),
            Some(("x.sslip.io".into(), 443))
        );
        assert_eq!(
            host_port("http://1.2.3.4:5000"),
            Some(("1.2.3.4".into(), 5000))
        );
        assert_eq!(host_port("nonsense"), None);
    }

    #[test]
    fn messages_match_net_status_h() {
        let h = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../shell-common/net_status.h"
        ))
        .unwrap();
        for s in [
            NoInternetReason::NoNetwork.headline(),
            NoInternetReason::NoNetwork.detail(),
            NoInternetReason::ServerUnreachable.headline(),
            NoInternetReason::ServerUnreachable.detail(),
            RETRY_SUFFIX,
        ] {
            assert!(h.contains(s), "net_status.h lacks: {s}");
        }
    }
}
