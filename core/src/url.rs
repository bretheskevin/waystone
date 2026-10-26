use alloc::string::{String, ToString};

/// `scheme://user:pass@host/…` -> `scheme://***@host/…`, so a URL is safe to log.
pub fn redact_userinfo(url: &str) -> String {
    let Some(scheme_end) = url.find("://").map(|i| i + 3) else {
        return url.to_string();
    };
    let rest = &url[scheme_end..];
    let authority_len = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    match rest[..authority_len].rfind('@') {
        Some(at) => format!("{}***{}", &url[..scheme_end], &rest[at..]),
        None => url.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::redact_userinfo;

    #[test]
    fn strips_credentials_only() {
        assert_eq!(
            redact_userinfo("https://bob:s3cr3t@dav.example.com:5000/ws?x=1"),
            "https://***@dav.example.com:5000/ws?x=1"
        );
        assert_eq!(
            redact_userinfo("http://bob@host/p@th"),
            "http://***@host/p@th"
        );
        assert_eq!(redact_userinfo("http://a:b@c@host"), "http://***@host");
    }

    #[test]
    fn leaves_plain_urls_untouched() {
        for u in ["http://213.0.0.1:5000", "https://h/a@b", "not a url", ""] {
            assert_eq!(redact_userinfo(u), u);
        }
    }
}
