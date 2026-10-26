use anyhow::{Context, Result};
use waystone_core::url::redact_userinfo;
use zeroize::Zeroize;

pub const SERVICE: &str = "dev.waystone.desktop";
const SESSION_KEY: &str = "session";

pub trait SecretStore: Send + Sync {
    fn get(&self, key: &str) -> Result<Option<String>>;
    fn set(&self, key: &str, value: &str) -> Result<()>;
    fn delete(&self, key: &str) -> Result<()>;
}

pub struct OsKeychain;

impl SecretStore for OsKeychain {
    fn get(&self, key: &str) -> Result<Option<String>> {
        match keyring::Entry::new(SERVICE, key)?.get_password() {
            Ok(v) => Ok(Some(v)),
            Err(keyring::Error::NoEntry) => Ok(None),
            Err(e) => Err(e).context("keychain read"),
        }
    }

    fn set(&self, key: &str, value: &str) -> Result<()> {
        keyring::Entry::new(SERVICE, key)?
            .set_password(value)
            .context("keychain write")
    }

    fn delete(&self, key: &str) -> Result<()> {
        match keyring::Entry::new(SERVICE, key)?.delete_credential() {
            Ok(()) | Err(keyring::Error::NoEntry) => Ok(()),
            Err(e) => Err(e).context("keychain delete"),
        }
    }
}

#[cfg(test)]
#[derive(Default)]
pub struct MemoryStore(std::sync::Mutex<std::collections::HashMap<String, String>>);

#[cfg(test)]
impl SecretStore for MemoryStore {
    fn get(&self, key: &str) -> Result<Option<String>> {
        Ok(self
            .0
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .get(key)
            .cloned())
    }

    fn set(&self, key: &str, value: &str) -> Result<()> {
        self.0
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .insert(key.into(), value.into());
        Ok(())
    }

    fn delete(&self, key: &str) -> Result<()> {
        self.0
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .remove(key);
        Ok(())
    }
}

#[derive(serde::Serialize, serde::Deserialize)]
pub struct StoredSession {
    pub server_url: String,
    pub username: Option<String>,
    pub mdk_hex: String,
    pub webdav_password: Option<String>,
}

impl std::fmt::Debug for StoredSession {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("StoredSession")
            .field("server_url", &self.server_url)
            .field("username", &self.username)
            .field("mdk_hex", &"[REDACTED]")
            .field("webdav_password", &"[REDACTED]")
            .finish()
    }
}

impl Drop for StoredSession {
    fn drop(&mut self) {
        self.mdk_hex.zeroize();
        if let Some(p) = self.webdav_password.as_mut() {
            p.zeroize();
        }
    }
}

impl StoredSession {
    pub fn mdk(&self) -> Result<[u8; 32]> {
        let mut bytes = hex::decode(&self.mdk_hex).context("session mdk is not hex")?;
        let arr: Result<[u8; 32], _> = bytes.as_slice().try_into();
        bytes.zeroize();
        arr.context("session mdk is not 32 bytes")
    }
}

pub fn save_session(store: &dyn SecretStore, s: &StoredSession) -> Result<()> {
    let mut json = serde_json::to_string(s)?;
    let r = store.set(SESSION_KEY, &json);
    json.zeroize();
    r?;
    log::info!(
        "[vault] session saved to keychain for {}",
        redact_userinfo(&s.server_url)
    );
    Ok(())
}

/// None when absent, unreadable, or for another server (a stale entry is deleted).
pub fn load_session(store: &dyn SecretStore, server_url: &str) -> Option<StoredSession> {
    let mut raw = match store.get(SESSION_KEY) {
        Ok(Some(r)) => r,
        Ok(None) => {
            log::info!("[vault] no keychain session");
            return None;
        }
        Err(e) => {
            log::warn!("[vault] keychain unavailable ({e:#}); falling back to prompts");
            return None;
        }
    };
    let parsed = serde_json::from_str::<StoredSession>(&raw);
    raw.zeroize();
    match parsed {
        Ok(s) if s.server_url == server_url => {
            log::info!(
                "[vault] keychain session found for {}",
                redact_userinfo(server_url)
            );
            Some(s)
        }
        Ok(s) => {
            log::info!(
                "[vault] keychain session is for {} (now {}) -- clearing",
                redact_userinfo(&s.server_url),
                redact_userinfo(server_url)
            );
            let _ = clear_session(store);
            None
        }
        Err(e) => {
            log::warn!("[vault] keychain session corrupt ({e}) -- clearing");
            let _ = clear_session(store);
            None
        }
    }
}

pub fn clear_session(store: &dyn SecretStore) -> Result<()> {
    store.delete(SESSION_KEY)?;
    log::info!("[vault] keychain session cleared");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn session(url: &str) -> StoredSession {
        StoredSession {
            server_url: url.into(),
            username: Some("kev".into()),
            mdk_hex: "ab".repeat(32),
            webdav_password: Some("hunter2".into()),
        }
    }

    #[test]
    fn round_trip() {
        let store = MemoryStore::default();
        save_session(&store, &session("https://a")).unwrap();
        let s = load_session(&store, "https://a").unwrap();
        assert_eq!(s.username.as_deref(), Some("kev"));
        assert_eq!(s.webdav_password.as_deref(), Some("hunter2"));
        assert_eq!(s.mdk().unwrap(), [0xab; 32]);
    }

    #[test]
    fn other_server_entry_is_cleared() {
        let store = MemoryStore::default();
        save_session(&store, &session("https://a")).unwrap();
        assert!(load_session(&store, "https://b").is_none());
        assert!(store.get(SESSION_KEY).unwrap().is_none());
    }

    #[test]
    fn corrupt_json_is_cleared() {
        let store = MemoryStore::default();
        store.set(SESSION_KEY, "{not json").unwrap();
        assert!(load_session(&store, "https://a").is_none());
        assert!(store.get(SESSION_KEY).unwrap().is_none());
    }

    #[test]
    fn mdk_rejects_wrong_length_and_non_hex() {
        let mut s = session("https://a");
        s.mdk_hex = "abcd".into();
        assert!(s.mdk().is_err());
        s.mdk_hex = "zz".into();
        assert!(s.mdk().is_err());
    }

    #[test]
    fn debug_has_no_secrets() {
        let s = session("https://a");
        let d = format!("{s:?}");
        assert!(!d.contains("hunter2"));
        assert!(!d.contains(&s.mdk_hex));
    }
}
