use crate::config::WaystoneConfig;
use crate::keychain::{SecretStore, StoredSession, load_session, save_session};
use crate::net_status::{self, NoInternetReason};
use crate::webdav::{BlockingWebDav, WebDavClient, make_blocking_dav};
use anyhow::{Context, Result};
use std::sync::Arc;
use waystone_core::crypto::Vault;
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
        Ok(mdk) => {
            log::info!("[vault] unlocked from keychain session");
            Some((Vault::from_mdk(mdk), session))
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
        mdk_hex: hex::encode(mdk),
        webdav_password,
    };
    mdk.zeroize();
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
            let mut passphrase = prompt("Passphrase: ")?;
            let pw = resolve_webdav_password(username.as_deref(), None, prompt);
            let result = match pw {
                Ok(pw) => unlock_from_server(&cfg.server_url, &username, &pw, &passphrase, false)
                    .await
                    .map(|v| (v, pw)),
                Err(e) => Err(e),
            };
            passphrase.zeroize();
            let (vault, pw) = result?;
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

/// Fetches `/keys.json` and unlocks it with the passphrase (or the recovery key); CLI + TUI.
pub async fn unlock_from_server(
    server_url: &str,
    username: &Option<String>,
    password: &Option<String>,
    secret: &str,
    recovery: bool,
) -> Result<Vault> {
    let dav = WebDavClient::new(server_url, username.clone(), password.clone());
    let keys = dav
        .get("/keys.json")
        .await?
        .context("no keys.json on server -- run `waystone init` first")?;
    let unlocked = if recovery {
        Vault::unlock_with_recovery(secret, &keys)
    } else {
        Vault::unlock_with_passphrase(secret, &keys)
    };
    let vault = unlocked.map_err(|e| {
        log::warn!("[vault] unlock failed (recovery={recovery}): {e}");
        anyhow::Error::from(e)
    })?;
    log::info!("[vault] unlocked (recovery={recovery})");
    Ok(vault)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::keychain::MemoryStore;

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

    #[tokio::test(flavor = "multi_thread")]
    async fn session_for_other_server_falls_back_to_prompt() {
        let store = MemoryStore::default();
        seed(&store, "http://other");
        let err = match open_with(&cfg("http://127.0.0.1:1"), None, &store, &|_| {
            anyhow::bail!("prompted")
        })
        .await
        {
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
    fn offline_display_is_headline_then_detail() {
        let s = Offline(NoInternetReason::NoNetwork).to_string();
        assert_eq!(
            s,
            "No internet connection detected.\nCheck your Wi-Fi settings and try again."
        );
    }
}
