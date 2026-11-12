use crate::config::WaystoneConfig;
use crate::keychain::{SecretStore, StoredSession, load_session, save_session};
use crate::net_status::{self, NoInternetReason};
use crate::webdav::{BlockingWebDav, WebDavClient, make_blocking_dav};
use anyhow::{Context, Result};
use std::sync::Arc;
use waystone_core::crypto::{KeysMode, Vault};
use waystone_core::url::redact_userinfo;
use zeroize::Zeroize;

/// The server (or the whole network) is unreachable; the CLI exits with code 2.
#[derive(Debug)]
pub struct Offline(pub NoInternetReason);

impl std::fmt::Display for Offline {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}\n{}", self.0.headline(), self.0.detail())
    }
}

impl std::error::Error for Offline {}

pub struct CliSession {
    pub vault: Arc<Vault>,
    /// Drop it on a blocking thread (reqwest::blocking panics when dropped inside the runtime).
    pub blocking: Arc<BlockingWebDav>,
}

pub async fn ensure_online(server_url: &str) -> Result<()> {
    let url = server_url.to_owned();
    match tokio::task::spawn_blocking(move || net_status::probe(&url)).await? {
        None => Ok(()),
        Some(reason) => Err(Offline(reason).into()),
    }
}

pub fn prompt_secret(label: &str) -> Result<String> {
    Ok(rpassword::prompt_password(label)?)
}

pub fn env_webdav_password() -> Option<String> {
    std::env::var("WAYSTONE_WEBDAV_PASSWORD").ok()
}

/// The keychain session for `server_url` and its vault; None when absent, foreign or unusable.
pub fn keychain_vault(store: &dyn SecretStore, server_url: &str) -> Option<(Vault, StoredSession)> {
    let session = load_session(store, server_url)?;
    match session.mdk() {
        Ok(Some(mdk)) => {
            log::info!("[vault] unlocked from keychain session");
            Some((Vault::from_mdk(mdk), session))
        }
        Ok(None) => {
            log::info!("[vault] unlocked from keychain session (unencrypted server)");
            Some((Vault::plain(), session))
        }
        Err(e) => {
            log::warn!("[vault] keychain session unusable ({e:#}); falling back to prompts");
            None
        }
    }
}

/// (username, stored WebDAV password) for a keychain session, shared by CLI and TUI: the
/// configured username wins, and the stored password is used only when it belongs to it.
pub fn session_identity(
    session: &StoredSession,
    configured: Option<String>,
) -> (Option<String>, Option<String>) {
    let username = configured.or_else(|| session.username.clone());
    let password = if session.username == username {
        session.webdav_password.clone()
    } else {
        log::info!(
            "[vault] keychain session belongs to another WebDAV user; ignoring its password"
        );
        None
    };
    (username, password)
}

/// WAYSTONE_WEBDAV_PASSWORD > `stored` > prompt (only when a username is set).
pub fn resolve_webdav_password(
    username: Option<&str>,
    stored: Option<String>,
    prompt: &dyn Fn(&str) -> Result<String>,
) -> Result<Option<String>> {
    let Some(user) = username else {
        return Ok(None);
    };
    if let Some(p) = env_webdav_password() {
        return Ok(Some(p));
    }
    if stored.is_some() {
        return Ok(stored);
    }
    Ok(Some(prompt(&format!("WebDAV password for {user}: "))?))
}

pub fn remember(
    store: &dyn SecretStore,
    server_url: &str,
    username: Option<String>,
    vault: &Vault,
    webdav_password: Option<String>,
) {
    let mut mdk = vault.export_mdk();
    let session = StoredSession {
        server_url: server_url.to_string(),
        username,
        mdk_hex: mdk.map(hex::encode),
        webdav_password,
    };
    mdk.zeroize();
    log::info!(
        "[vault] remembering session (encrypted={})",
        vault.is_encrypted()
    );
    if let Err(e) = save_session(store, &session) {
        log::warn!(
            "[vault] could not save keychain session ({e:#}); you will be prompted next time"
        );
    }
}

/// Online check, then keychain session > prompts; saves the session after a prompted unlock.
pub async fn open(
    cfg: &WaystoneConfig,
    username_override: Option<String>,
    store: &dyn SecretStore,
) -> Result<CliSession> {
    ensure_online(&cfg.server_url).await?;
    open_with(cfg, username_override, store, &prompt_secret).await
}

pub async fn open_with(
    cfg: &WaystoneConfig,
    username_override: Option<String>,
    store: &dyn SecretStore,
    prompt: &dyn Fn(&str) -> Result<String>,
) -> Result<CliSession> {
    let mut username = username_override.or_else(|| cfg.username.clone());
    let (vault, password) = match keychain_vault(store, &cfg.server_url) {
        Some((vault, s)) => {
            let (user, stored) = session_identity(&s, username);
            username = user;
            let had_stored = stored.is_some();
            let pw = resolve_webdav_password(username.as_deref(), stored, prompt)?;
            if !had_stored && pw.is_some() {
                remember(store, &cfg.server_url, username.clone(), &vault, pw.clone());
            }
            (vault, pw)
        }
        None => {
            let pw = resolve_webdav_password(username.as_deref(), None, prompt)?;
            let vault = unlock_from_server(&cfg.server_url, &username, &pw, false, || {
                prompt("Passphrase: ")
            })
            .await?;
            remember(store, &cfg.server_url, username.clone(), &vault, pw.clone());
            (vault, pw)
        }
    };
    let blocking = make_blocking_dav(&cfg.server_url, username, password).await?;
    Ok(CliSession {
        vault: Arc::new(vault),
        blocking: Arc::new(blocking),
    })
}

/// Fetches `/keys.json` once and opens it; `secret` (passphrase or recovery key) is asked
/// only for an encrypted server. CLI + TUI.
pub async fn unlock_from_server(
    server_url: &str,
    username: &Option<String>,
    password: &Option<String>,
    recovery: bool,
    mut secret: impl FnMut() -> Result<String>,
) -> Result<Vault> {
    log::info!(
        "[vault] unlock: fetching /keys.json from {}",
        redact_userinfo(server_url)
    );
    let dav = WebDavClient::new(server_url, username.clone(), password.clone());
    let keys = dav.get("/keys.json").await.inspect_err(|e| {
        log::warn!("[vault] unlock: fetching /keys.json failed: {e:#}");
    })?;
    open_keys(keys, recovery, &mut secret)
}

fn open_keys(
    keys: Option<Vec<u8>>,
    recovery: bool,
    secret: &mut dyn FnMut() -> Result<String>,
) -> Result<Vault> {
    let keys = keys.context("no keys.json on server -- run `waystone init` first")?;
    let opened = match crate::vault_ops::detect_mode(&keys)? {
        KeysMode::Plain => Vault::open(&keys, None, recovery),
        KeysMode::Encrypted => {
            let mut s = secret()?;
            let r = Vault::open(&keys, Some(&s), recovery);
            s.zeroize();
            r
        }
    };
    let vault = opened.map_err(|e| {
        log::warn!("[vault] unlock failed (recovery={recovery}): {e}");
        anyhow::Error::from(e)
    })?;
    log::info!(
        "[vault] unlocked (recovery={recovery}, encrypted={})",
        vault.is_encrypted()
    );
    Ok(vault)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::keychain::MemoryStore;
    use waystone_core::crypto::PLAIN_KEYS_JSON;

    fn cfg(url: &str) -> WaystoneConfig {
        WaystoneConfig {
            server_url: url.into(),
            ..Default::default()
        }
    }

    fn seed(store: &MemoryStore, url: &str) {
        let vault = Vault::init("pw").unwrap().0;
        remember(store, url, None, &vault, None);
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn session_for_same_server_never_prompts() {
        let store = MemoryStore::default();
        seed(&store, "http://127.0.0.1:1");
        let s = open_with(&cfg("http://127.0.0.1:1"), None, &store, &|_| {
            panic!("must not prompt")
        })
        .await
        .unwrap();
        tokio::task::spawn_blocking(move || drop(s)).await.unwrap();
    }

    #[test]
    fn keychain_vault_of_plain_session_is_unencrypted() {
        let store = MemoryStore::default();
        remember(
            &store,
            "http://a",
            Some("bob".into()),
            &Vault::plain(),
            Some("dav".into()),
        );
        let (vault, s) = keychain_vault(&store, "http://a").expect("plain session restores");
        assert!(!vault.is_encrypted());
        assert_eq!(s.webdav_password.as_deref(), Some("dav"));
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn plain_session_for_same_server_never_prompts() {
        let store = MemoryStore::default();
        remember(&store, "http://127.0.0.1:1", None, &Vault::plain(), None);
        let s = open_with(&cfg("http://127.0.0.1:1"), None, &store, &|_| {
            panic!("must not prompt")
        })
        .await
        .unwrap();
        assert!(!s.vault.is_encrypted());
        tokio::task::spawn_blocking(move || drop(s)).await.unwrap();
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn session_for_other_server_falls_back_to_prompt() {
        let store = MemoryStore::default();
        seed(&store, "http://other");
        let c = WaystoneConfig {
            username: Some("bob".into()),
            ..cfg("http://127.0.0.1:1")
        };
        let err = match open_with(&c, None, &store, &|_| anyhow::bail!("prompted")).await {
            Ok(_) => panic!("expected the prompt to be used"),
            Err(e) => e,
        };
        assert!(err.to_string().contains("prompted"), "{err}");
        assert!(load_session(&store, "http://other").is_none());
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn prompted_webdav_password_is_remembered_for_next_run() {
        let store = MemoryStore::default();
        let vault = Vault::init("pw").unwrap().0;
        remember(
            &store,
            "http://127.0.0.1:1",
            Some("bob".into()),
            &vault,
            None,
        );
        let c = WaystoneConfig {
            username: Some("bob".into()),
            ..cfg("http://127.0.0.1:1")
        };
        let s = open_with(&c, None, &store, &|_| Ok("dav-pw".into()))
            .await
            .unwrap();
        tokio::task::spawn_blocking(move || drop(s)).await.unwrap();
        let stored = load_session(&store, "http://127.0.0.1:1").unwrap();
        assert_eq!(stored.webdav_password.as_deref(), Some("dav-pw"));
    }

    #[test]
    fn open_keys_on_plain_server_never_asks_for_a_secret() {
        let vault = open_keys(Some(PLAIN_KEYS_JSON.to_vec()), false, &mut || {
            panic!("must not ask for a secret")
        })
        .unwrap();
        assert!(!vault.is_encrypted());
    }

    #[test]
    fn open_keys_on_encrypted_server_asks_exactly_once() {
        let keys = Vault::init("pw").unwrap().0.keys_json().unwrap();
        let mut calls = 0;
        let vault = open_keys(Some(keys), false, &mut || {
            calls += 1;
            Ok("pw".into())
        })
        .unwrap();
        assert_eq!(calls, 1);
        assert!(vault.is_encrypted());
    }

    #[test]
    fn open_keys_without_keys_json_says_run_init() {
        let err = open_keys(None, false, &mut || panic!("no secret"))
            .err()
            .unwrap();
        assert!(
            err.to_string().contains("run `waystone init` first"),
            "{err}"
        );
    }

    #[test]
    fn offline_display_is_headline_then_detail() {
        let s = Offline(NoInternetReason::NoNetwork).to_string();
        assert_eq!(
            s,
            "No internet connection detected.\nCheck your Wi-Fi settings and try again."
        );
    }
}
