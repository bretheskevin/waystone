use std::io::{Read, Write};

pub const RELEASES_API: &str = "https://api.github.com/repos/bretheskevin/waystone/releases/latest";
pub const USER_AGENT: &str = "waystone";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Release {
    pub version: String,
    pub asset_url: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CheckResult {
    UpToDate(String),
    Available(Release),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UpdateError {
    Net,
    GitHub,
    Parse,
    NoAsset,
    Install,
    Io,
}

impl UpdateError {
    pub fn check_message(self) -> String {
        match self {
            Self::Net => "No connection \u{2014} couldn't check for updates".into(),
            Self::GitHub => "GitHub error or rate-limited \u{2014} try again later".into(),
            Self::NoAsset => format!("Latest release has no {} build", asset_name()),
            _ => "Unexpected GitHub response".into(),
        }
    }

    pub fn install_message(self) -> &'static str {
        match self {
            Self::Net => "Download failed \u{2014} connection lost",
            Self::Install => "Install failed \u{2014} current version kept",
            _ => "Write failed \u{2014} current version kept",
        }
    }
}

fn exe_suffix() -> &'static str {
    if cfg!(windows) { ".exe" } else { "" }
}

pub fn asset_name() -> String {
    format!(
        "waystone-desktop-{}{}",
        env!("WAYSTONE_TARGET"),
        exe_suffix()
    )
}

pub fn parse_release(json: &str, asset: &str) -> Result<Release, UpdateError> {
    let v: serde_json::Value = serde_json::from_str(json).map_err(|_| UpdateError::Parse)?;
    let tag = v["tag_name"].as_str().ok_or(UpdateError::Parse)?;
    let version = tag.strip_prefix('v').unwrap_or(tag).to_string();
    let url = v["assets"]
        .as_array()
        .ok_or(UpdateError::Parse)?
        .iter()
        .find(|a| a["name"].as_str() == Some(asset))
        .and_then(|a| a["browser_download_url"].as_str())
        .ok_or(UpdateError::NoAsset)?;
    Ok(Release {
        version,
        asset_url: url.to_string(),
    })
}

fn client() -> Result<reqwest::blocking::Client, UpdateError> {
    reqwest::blocking::Client::builder()
        .user_agent(USER_AGENT)
        .connect_timeout(std::time::Duration::from_secs(5))
        .timeout(std::time::Duration::from_secs(120))
        .build()
        .map_err(|e| {
            log::warn!("[update] HTTP client init failed: {e}");
            UpdateError::Net
        })
}

pub fn check(current: &str) -> Result<CheckResult, UpdateError> {
    log::info!("[update] check: current v{current}, asset {}", asset_name());
    let resp = client()?.get(RELEASES_API).send().map_err(|e| {
        log::warn!("[update] check net error: {e}");
        UpdateError::Net
    })?;
    if !resp.status().is_success() {
        log::warn!("[update] check HTTP {}", resp.status());
        return Err(UpdateError::GitHub);
    }
    let body = resp.text().map_err(|e| {
        log::warn!("[update] check body read failed: {e}");
        UpdateError::Net
    })?;
    let rel = parse_release(&body, &asset_name())
        .inspect_err(|e| log::warn!("[update] parse failed: {e:?}"))?;
    if waystone_core::version::version_newer(&rel.version, current) {
        log::info!("[update] v{} available ({})", rel.version, rel.asset_url);
        Ok(CheckResult::Available(rel))
    } else {
        log::info!(
            "[update] up to date (latest v{}, current v{current})",
            rel.version
        );
        Ok(CheckResult::UpToDate(rel.version))
    }
}

/// Downloads to a temp file and swaps it over the running executable (Windows-safe via self-replace).
/// `progress` fires once per whole percent (per 64 KiB when the size is unknown).
pub fn install(
    rel: &Release,
    mut progress: impl FnMut(u64, Option<u64>),
) -> Result<(), UpdateError> {
    // Per-install name: a second install in this process must never truncate the first's file.
    static INSTALL_SEQ: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
    let seq = INSTALL_SEQ.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
    let tmp = std::env::temp_dir().join(format!(
        "waystone-update-{}-{seq}{}",
        std::process::id(),
        exe_suffix()
    ));
    log::info!("[update] download {} -> {}", rel.asset_url, tmp.display());
    let result = (|| {
        let mut resp = client()?.get(&rel.asset_url).send().map_err(|e| {
            log::warn!("[update] download request failed: {e}");
            UpdateError::Net
        })?;
        if !resp.status().is_success() {
            log::warn!("[update] download HTTP {}", resp.status());
            return Err(UpdateError::Net);
        }
        let total = resp.content_length();
        let _ = std::fs::remove_file(&tmp);
        // create_new: never follow a pre-planted symlink in a shared temp dir.
        let mut f = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&tmp)
            .map_err(|e| {
                log::warn!("[update] create {} failed: {e}", tmp.display());
                UpdateError::Io
            })?;
        let mut buf = vec![0u8; 64 * 1024];
        let mut done = 0u64;
        let mut last_step = u64::MAX;
        loop {
            let n = resp.read(&mut buf).map_err(|e| {
                log::warn!("[update] download read failed after {done} bytes: {e}");
                UpdateError::Net
            })?;
            if n == 0 {
                break;
            }
            f.write_all(&buf[..n]).map_err(|e| {
                log::warn!("[update] write failed: {e}");
                UpdateError::Io
            })?;
            done += n as u64;
            let step = total
                .filter(|t| *t > 0)
                .map_or(done / 65536, |t| done * 100 / t);
            if step != last_step {
                last_step = step;
                progress(done, total);
            }
        }
        drop(f);
        if total.is_some_and(|t| t != done) {
            log::warn!("[update] truncated download: {done} of {total:?} bytes");
            return Err(UpdateError::Net);
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&tmp, std::fs::Permissions::from_mode(0o755)).map_err(
                |e| {
                    log::warn!("[update] chmod {} failed: {e}", tmp.display());
                    UpdateError::Io
                },
            )?;
        }
        self_replace::self_replace(&tmp).map_err(|e| {
            log::warn!("[update] self_replace failed: {e}");
            UpdateError::Install
        })
    })();
    let _ = std::fs::remove_file(&tmp);
    match &result {
        Ok(()) => log::info!("[update] installed v{} -- restart to use it", rel.version),
        Err(e) => log::warn!("[update] install failed: {e:?}"),
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(tag: &str) -> String {
        format!(
            r#"{{"tag_name":"{tag}","assets":[
              {{"name":"waystone.apk","browser_download_url":"https://x/apk"}},
              {{"name":"waystone.nro","browser_download_url":"https://x/nro"}},
              {{"name":"{}","browser_download_url":"https://x/desktop"}}]}}"#,
            asset_name()
        )
    }

    #[test]
    fn picks_exact_asset_and_strips_v() {
        let r = parse_release(&fixture("v1.2.3"), &asset_name()).unwrap();
        assert_eq!(r.version, "1.2.3");
        assert_eq!(r.asset_url, "https://x/desktop");
    }

    #[test]
    fn tag_without_v_is_kept() {
        let r = parse_release(&fixture("1.2.3"), &asset_name()).unwrap();
        assert_eq!(r.version, "1.2.3");
    }

    #[test]
    fn missing_asset_is_no_asset() {
        assert_eq!(
            parse_release(&fixture("v1.0.0"), "waystone-desktop-nope"),
            Err(UpdateError::NoAsset)
        );
    }

    #[test]
    fn malformed_is_parse_error() {
        assert_eq!(parse_release("{oops", "a"), Err(UpdateError::Parse));
        assert_eq!(
            parse_release(r#"{"assets":[]}"#, "a"),
            Err(UpdateError::Parse)
        );
    }

    #[test]
    fn asset_name_shape() {
        let n = asset_name();
        assert!(n.starts_with("waystone-desktop-"));
        assert_eq!(n.ends_with(".exe"), cfg!(windows));
    }

    #[test]
    fn messages_match_shell_common_updater() {
        let src = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../shell-common/updater.cpp"
        ))
        .unwrap();
        let src = src.replace("\\xe2\\x80\\x94", "\u{2014}");
        for s in [
            UpdateError::Net.check_message(),
            UpdateError::GitHub.check_message(),
            UpdateError::Parse.check_message(),
            UpdateError::Net.install_message().to_string(),
        ] {
            assert!(src.contains(&s), "updater.cpp lacks: {s}");
        }
    }
}
