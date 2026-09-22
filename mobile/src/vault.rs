use crate::error::WaystoneError;
use std::sync::Arc;

#[derive(uniffi::Record)]
pub struct VaultInit {
    pub vault: Arc<Vault>,
    pub recovery_hex: String,
    pub keys_json: Vec<u8>,
}

#[derive(uniffi::Object)]
pub struct Vault {
    inner: waystone_core::crypto::Vault,
}

impl Vault {
    pub fn init(passphrase: String) -> Result<VaultInit, WaystoneError> {
        let (vault, recovery_hex) = waystone_core::crypto::Vault::init(&passphrase)?;
        let keys_json = vault.keys_json()?;
        Ok(VaultInit {
            vault: Arc::new(Vault { inner: vault }),
            recovery_hex,
            keys_json,
        })
    }

    pub(crate) fn core_vault(&self) -> &waystone_core::crypto::Vault {
        &self.inner
    }
}

#[uniffi::export]
pub fn vault_init(passphrase: String) -> Result<VaultInit, WaystoneError> {
    Vault::init(passphrase)
}

#[uniffi::export]
impl Vault {
    #[uniffi::constructor]
    pub fn unlock_with_passphrase(
        passphrase: String,
        keys_json: Vec<u8>,
    ) -> Result<Arc<Self>, WaystoneError> {
        let vault = waystone_core::crypto::Vault::unlock_with_passphrase(&passphrase, &keys_json)?;
        Ok(Arc::new(Vault { inner: vault }))
    }

    #[uniffi::constructor]
    pub fn unlock_with_recovery(
        recovery_hex: String,
        keys_json: Vec<u8>,
    ) -> Result<Arc<Self>, WaystoneError> {
        let vault = waystone_core::crypto::Vault::unlock_with_recovery(&recovery_hex, &keys_json)?;
        Ok(Arc::new(Vault { inner: vault }))
    }

    pub fn encrypt_blob(&self, data: Vec<u8>) -> Result<Vec<u8>, WaystoneError> {
        Ok(self.inner.encrypt_blob(&data)?)
    }

    pub fn decrypt_blob(&self, data: Vec<u8>) -> Result<Vec<u8>, WaystoneError> {
        Ok(self.inner.decrypt_blob(&data)?)
    }

    pub fn encrypt_heads(&self, data: Vec<u8>) -> Result<Vec<u8>, WaystoneError> {
        Ok(self.inner.encrypt_heads(&data)?)
    }

    pub fn decrypt_heads(&self, data: Vec<u8>) -> Result<Vec<u8>, WaystoneError> {
        Ok(self.inner.decrypt_heads(&data)?)
    }

    pub fn blob_name(&self, hash: String) -> String {
        self.inner.blob_name(&hash)
    }

    pub fn path_segment(&self, name: String) -> String {
        self.inner.path_segment(&name)
    }

    pub fn keys_json(&self) -> Result<Vec<u8>, WaystoneError> {
        Ok(self.inner.keys_json()?)
    }

    pub fn export_mdk(&self) -> Vec<u8> {
        self.inner.export_mdk().to_vec()
    }
}

#[uniffi::export]
pub fn vault_from_mdk(mdk: Vec<u8>) -> Result<Arc<Vault>, WaystoneError> {
    let arr: [u8; 32] = mdk.try_into().map_err(|_| WaystoneError::Crypto {
        msg: "mdk must be 32 bytes".into(),
    })?;
    Ok(Arc::new(Vault {
        inner: waystone_core::crypto::Vault::from_mdk(arr),
    }))
}
