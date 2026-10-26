use crate::webdav::WebDavClient;
use waystone_core::crypto::Vault;

pub const VAULT_EXISTS: &str =
    "A vault already exists on this server. Use Unlock instead of Setup.";

#[derive(Debug)]
pub struct VaultExists;

impl std::fmt::Display for VaultExists {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(VAULT_EXISTS)
    }
}

impl std::error::Error for VaultExists {}

/// GET /keys.json guard -> init -> PUT. Never overwrites an existing vault (it would orphan every blob).
pub async fn init_vault(dav: &WebDavClient, passphrase: &str) -> anyhow::Result<(Vault, String)> {
    log::info!("[vault] init: checking server for an existing /keys.json");
    if dav.get("/keys.json").await?.is_some() {
        log::warn!("[vault] init refused: /keys.json already exists");
        return Err(VaultExists.into());
    }
    log::info!("[vault] init: deriving keys (Argon2)");
    let (vault, recovery) = Vault::init(passphrase)?;
    dav.mkdir_p("/").await?;
    dav.put("/keys.json", vault.keys_json()?).await?;
    log::info!("[vault] initialized new vault on server");
    Ok((vault, recovery))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn refusal_message_is_the_shared_constant() {
        assert_eq!(VaultExists.to_string(), VAULT_EXISTS);
    }
}
