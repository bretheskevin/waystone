fn component(s: &[u8], i: &mut usize) -> u64 {
    let mut v: u64 = 0;
    while *i < s.len() && s[*i].is_ascii_digit() {
        v = v.saturating_mul(10).saturating_add(u64::from(s[*i] - b'0'));
        *i += 1;
    }
    v
}

/// Numeric dot-separated compare: `1.10.0` > `1.9.0`; malformed components count as 0.
pub fn version_newer(latest: &str, current: &str) -> bool {
    let (l, c) = (latest.as_bytes(), current.as_bytes());
    let (mut i, mut j) = (0usize, 0usize);
    loop {
        let lv = component(l, &mut i);
        let cv = component(c, &mut j);
        if lv != cv {
            return lv > cv;
        }
        if i >= l.len() && j >= c.len() {
            return false;
        }
        if i < l.len() {
            i += 1;
        }
        if j < c.len() {
            j += 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::version_newer;

    #[test]
    fn matches_shell_common_cases() {
        assert!(version_newer("1.10.0", "1.9.0"));
        assert!(!version_newer("1.9.0", "1.10.0"));
        assert!(version_newer("0.2.0", "0.1.0"));
        assert!(version_newer("2", "1.9.9"));
        assert!(version_newer("1.0.1", "1.0"));
        assert!(!version_newer("1.0.0", "1.0.0"));
        assert!(!version_newer("1.0", "1.0.0"));
        assert!(!version_newer("1.0.0", "1.0"));
        assert!(!version_newer("1.0.0", "1.0.0.0"));
        assert!(!version_newer("abc", "1.0.0"));
        assert!(!version_newer("", ""));
        assert!(!version_newer("1.0a", "1.0"));
    }
}
