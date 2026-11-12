use crate::webdav::WebDavClient;
use waystone_core::crypto::{KeysMode, Vault, keys_mode};

pub const VAULT_EXISTS: &str =
    "A vault already exists on this server. Use Unlock instead of Setup.";

pub const PASSPHRASE_REQUIRED: &str = "Passphrase is required.";

#[derive(Debug)]
pub struct VaultExists;

impl std::fmt::Display for VaultExists {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(VAULT_EXISTS)
    }
}

impl std::error::Error for VaultExists {}

pub enum InitOutcome {
    Created {
        vault: Vault,
        recovery: Option<String>,
    },
    JoinedPlain {
        vault: Vault,
    },
}

impl InitOutcome {
    /// (vault, recovery key of a new encrypted vault, joined an existing plain server).
    pub fn into_parts(self) -> (Vault, Option<String>, bool) {
        match self {
            InitOutcome::Created { vault, recovery } => (vault, recovery, false),
            InitOutcome::JoinedPlain { vault } => (vault, None, true),
        }
    }
}

/// The success line shown by CLI `init` and the TUI setup.
pub fn setup_done_message(created_encrypted: bool, joined: bool) -> &'static str {
    match (created_encrypted, joined) {
        (true, _) => "Vault initialized.",
        (false, true) => "Joined the server (unencrypted).",
        (false, false) => "Server set up without encryption.",
    }
}

/// The one keys.json mode probe (logged) shared by init and unlock.
pub fn detect_mode(keys_json: &[u8]) -> anyhow::Result<KeysMode> {
    match keys_mode(keys_json) {
        Ok(mode) => {
            let name = match mode {
                KeysMode::Plain => "plain",
                KeysMode::Encrypted => "encrypted",
            };
            log::info!("[vault] keys.json mode={name}");
            Ok(mode)
        }
        Err(e) => {
            log::warn!("[vault] keys.json unreadable: {e}");
            Err(e.into())
        }
    }
}

/// Decides init from the server's current keys.json; the second value is what to PUT, if anything.
fn plan_init(
    existing: Option<&[u8]>,
    passphrase: Option<&str>,
) -> anyhow::Result<(InitOutcome, Option<Vec<u8>>)> {
    if let Some(keys) = existing {
        return match detect_mode(keys)? {
            KeysMode::Plain => {
                if passphrase.is_some() {
                    log::info!("[vault] server is unencrypted; joining without encryption");
                }
                log::info!("[vault] init: joined existing unencrypted server");
                Ok((
                    InitOutcome::JoinedPlain {
                        vault: Vault::plain(),
                    },
                    None,
                ))
            }
            KeysMode::Encrypted => {
                log::warn!("[vault] init refused: an encrypted /keys.json already exists");
                Err(VaultExists.into())
            }
        };
    }
    let (vault, recovery) = match passphrase {
        Some(p) => {
            log::info!("[vault] init: creating encrypted vault (Argon2)");
            let (vault, recovery) = Vault::init(p).inspect_err(|e| {
                log::warn!("[vault] init: key derivation failed: {e}");
            })?;
            (vault, Some(recovery))
        }
        None => {
            log::info!("[vault] init: creating unencrypted server");
            (Vault::plain(), None)
        }
    };
    let keys = vault.keys_json()?;
    Ok((InitOutcome::Created { vault, recovery }, Some(keys)))
}

/// GET /keys.json -> join a plain server, refuse an encrypted one (re-init would orphan every
/// blob), or create (plain without a passphrase, encrypted with one) and PUT.
pub async fn init_vault(
    dav: &WebDavClient,
    passphrase: Option<&str>,
) -> anyhow::Result<InitOutcome> {
    log::info!("[vault] init: checking server for an existing /keys.json");
    let existing = dav.get("/keys.json").await?;
    let (outcome, put) = plan_init(existing.as_deref(), passphrase)?;
    if let Some(keys) = put {
        dav.mkdir_p("/").await?;
        if let Err(e) = dav.put("/keys.json", keys).await {
            log::warn!("[vault] init: writing /keys.json failed: {e:#}");
            return Err(e);
        }
        log::info!(
            "[vault] initialized new {} server",
            if passphrase.is_some() {
                "encrypted"
            } else {
                "unencrypted"
            }
        );
    }
    Ok(outcome)
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::crypto::PLAIN_KEYS_JSON;

    #[test]
    fn refusal_message_is_the_shared_constant() {
        assert_eq!(VaultExists.to_string(), VAULT_EXISTS);
    }

    #[test]
    fn setup_done_message_per_outcome() {
        assert_eq!(setup_done_message(true, false), "Vault initialized.");
        assert_eq!(
            setup_done_message(false, true),
            "Joined the server (unencrypted)."
        );
        assert_eq!(
            setup_done_message(false, false),
            "Server set up without encryption."
        );
    }

    #[test]
    fn absent_without_passphrase_puts_plain_keys() {
        let (outcome, put) = plan_init(None, None).unwrap();
        assert_eq!(put.as_deref(), Some(PLAIN_KEYS_JSON));
        match outcome {
            InitOutcome::Created { vault, recovery } => {
                assert!(!vault.is_encrypted());
                assert!(recovery.is_none());
            }
            InitOutcome::JoinedPlain { .. } => panic!("expected Created"),
        }
    }

    #[test]
    fn absent_with_passphrase_creates_encrypted_vault_with_recovery() {
        let (outcome, put) = plan_init(None, Some("pw")).unwrap();
        let put = put.expect("keys.json must be written");
        assert_eq!(keys_mode(&put).unwrap(), KeysMode::Encrypted);
        match outcome {
            InitOutcome::Created { vault, recovery } => {
                assert!(vault.is_encrypted());
                assert_eq!(recovery.expect("recovery key").len(), 64);
            }
            InitOutcome::JoinedPlain { .. } => panic!("expected Created"),
        }
    }

    #[test]
    fn existing_plain_server_is_joined_without_writing() {
        for passphrase in [None, Some("pw")] {
            let (outcome, put) = plan_init(Some(PLAIN_KEYS_JSON), passphrase).unwrap();
            assert!(put.is_none());
            assert!(matches!(outcome, InitOutcome::JoinedPlain { vault } if !vault.is_encrypted()));
        }
    }

    #[test]
    fn into_parts_flags_joined_and_recovery() {
        let (v, rec, joined) = InitOutcome::JoinedPlain {
            vault: Vault::plain(),
        }
        .into_parts();
        assert!(!v.is_encrypted() && rec.is_none() && joined);
        let (_, rec, joined) = InitOutcome::Created {
            vault: Vault::plain(),
            recovery: Some("r".into()),
        }
        .into_parts();
        assert_eq!(rec.as_deref(), Some("r"));
        assert!(!joined);
    }

    #[test]
    fn existing_encrypted_server_is_refused() {
        let keys = Vault::init("pw").unwrap().0.keys_json().unwrap();
        let err = plan_init(Some(&keys), None).err().expect("refused");
        assert!(err.is::<VaultExists>());
    }
}
