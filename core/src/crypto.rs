use alloc::string::String;
use alloc::vec::Vec;

use chacha20poly1305::{
    Key, XChaCha20Poly1305, XNonce,
    aead::{Aead, KeyInit},
};
use hkdf::Hkdf;
use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use zeroize::Zeroize;

#[derive(Debug)]
pub enum CryptoError {
    KdfError,
    EncryptionFailed,
    DecryptionFailed,
    Json(serde_json::Error),
    Hex(hex::FromHexError),
    InvalidCiphertext,
    Rng,
}

impl core::fmt::Display for CryptoError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::KdfError => f.write_str("key derivation failed"),
            Self::EncryptionFailed => f.write_str("encryption failed"),
            Self::DecryptionFailed => f.write_str("decryption failed"),
            Self::Json(e) => write!(f, "json error: {e}"),
            Self::Hex(e) => write!(f, "hex error: {e}"),
            Self::InvalidCiphertext => f.write_str("invalid ciphertext"),
            Self::Rng => f.write_str("random number generator failed"),
        }
    }
}

impl core::error::Error for CryptoError {}

impl From<serde_json::Error> for CryptoError {
    fn from(e: serde_json::Error) -> Self {
        Self::Json(e)
    }
}

impl From<hex::FromHexError> for CryptoError {
    fn from(e: hex::FromHexError) -> Self {
        Self::Hex(e)
    }
}

fn fill_random(buf: &mut [u8]) -> Result<(), CryptoError> {
    getrandom::getrandom(buf).map_err(|_| CryptoError::Rng)
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct KeysFile {
    pub salt: String,
    pub wrapped_mdk_pass: String,
    pub wrapped_mdk_rec: String,
}

pub struct Vault {
    mdk: [u8; 32],
    keys_file: KeysFile,
}

fn derive_kek_from_passphrase(passphrase: &str, salt: &[u8]) -> Result<[u8; 32], CryptoError> {
    use argon2::Argon2;
    let mut kek = [0u8; 32];
    Argon2::default()
        .hash_password_into(passphrase.as_bytes(), salt, &mut kek)
        .map_err(|_| CryptoError::KdfError)?;
    Ok(kek)
}

fn derive_kek_from_recovery(recovery_bytes: &[u8], salt: &[u8]) -> [u8; 32] {
    let hk = Hkdf::<Sha256>::new(Some(salt), recovery_bytes);
    let mut kek = [0u8; 32];
    hk.expand(b"recovery-kek", &mut kek).expect("hkdf expand");
    kek
}

fn wrap_mdk(kek: &[u8; 32], mdk: &[u8; 32]) -> Result<Vec<u8>, CryptoError> {
    let cipher = XChaCha20Poly1305::new(Key::from_slice(kek));
    let mut nonce_bytes = [0u8; 24];
    fill_random(&mut nonce_bytes)?;
    let nonce = XNonce::from_slice(&nonce_bytes);
    let ct = cipher
        .encrypt(nonce, mdk.as_ref())
        .map_err(|_| CryptoError::EncryptionFailed)?;
    let mut out = nonce_bytes.to_vec();
    out.extend(ct);
    Ok(out)
}

fn unwrap_mdk(kek: &[u8; 32], wrapped: &[u8]) -> Result<[u8; 32], CryptoError> {
    if wrapped.len() < 24 {
        return Err(CryptoError::InvalidCiphertext);
    }
    let cipher = XChaCha20Poly1305::new(Key::from_slice(kek));
    let nonce = XNonce::from_slice(&wrapped[..24]);
    let pt = cipher
        .decrypt(nonce, &wrapped[24..])
        .map_err(|_| CryptoError::DecryptionFailed)?;
    if pt.len() != 32 {
        return Err(CryptoError::InvalidCiphertext);
    }
    let mut mdk = [0u8; 32];
    mdk.copy_from_slice(&pt);
    Ok(mdk)
}

fn derive_purpose_key(mdk: &[u8; 32], purpose: &[u8]) -> [u8; 32] {
    let hk = Hkdf::<Sha256>::new(None, mdk);
    let mut key = [0u8; 32];
    hk.expand(purpose, &mut key).expect("hkdf expand");
    key
}

fn xchacha_encrypt(key: &[u8; 32], data: &[u8]) -> Result<Vec<u8>, CryptoError> {
    let cipher = XChaCha20Poly1305::new(Key::from_slice(key));
    let mut nonce_bytes = [0u8; 24];
    fill_random(&mut nonce_bytes)?;
    let nonce = XNonce::from_slice(&nonce_bytes);
    let ct = cipher
        .encrypt(nonce, data)
        .map_err(|_| CryptoError::EncryptionFailed)?;
    let mut out = nonce_bytes.to_vec();
    out.extend(ct);
    Ok(out)
}

fn xchacha_decrypt(key: &[u8; 32], data: &[u8]) -> Result<Vec<u8>, CryptoError> {
    if data.len() < 24 {
        return Err(CryptoError::InvalidCiphertext);
    }
    let cipher = XChaCha20Poly1305::new(Key::from_slice(key));
    let nonce = XNonce::from_slice(&data[..24]);
    cipher
        .decrypt(nonce, &data[24..])
        .map_err(|_| CryptoError::DecryptionFailed)
}

fn hmac_name(mdk: &[u8; 32], purpose: &[u8], input: &str) -> String {
    let hmac_key = derive_purpose_key(mdk, purpose);
    let mut mac = <Hmac<Sha256> as Mac>::new_from_slice(&hmac_key).expect("hmac key");
    mac.update(input.as_bytes());
    hex::encode(mac.finalize().into_bytes())
}

impl Vault {
    pub fn init(passphrase: &str) -> Result<(Vault, String), CryptoError> {
        let mut salt = [0u8; 16];
        fill_random(&mut salt)?;

        let mut mdk = [0u8; 32];
        fill_random(&mut mdk)?;

        let pass_kek = derive_kek_from_passphrase(passphrase, &salt)?;

        let mut recovery_bytes = [0u8; 32];
        fill_random(&mut recovery_bytes)?;
        let rec_kek = derive_kek_from_recovery(&recovery_bytes, &salt);

        let wrapped_mdk_pass = wrap_mdk(&pass_kek, &mdk)?;
        let wrapped_mdk_rec = wrap_mdk(&rec_kek, &mdk)?;

        let keys_file = KeysFile {
            salt: hex::encode(salt),
            wrapped_mdk_pass: hex::encode(&wrapped_mdk_pass),
            wrapped_mdk_rec: hex::encode(&wrapped_mdk_rec),
        };

        Ok((Vault { mdk, keys_file }, hex::encode(recovery_bytes)))
    }

    pub fn keys_json(&self) -> Result<Vec<u8>, CryptoError> {
        Ok(serde_json::to_vec(&self.keys_file)?)
    }

    pub fn unlock_with_passphrase(
        passphrase: &str,
        keys_json: &[u8],
    ) -> Result<Vault, CryptoError> {
        let keys_file: KeysFile = serde_json::from_slice(keys_json)?;
        let salt = hex::decode(&keys_file.salt)?;
        let wrapped = hex::decode(&keys_file.wrapped_mdk_pass)?;
        let pass_kek = derive_kek_from_passphrase(passphrase, &salt)?;
        let mdk = unwrap_mdk(&pass_kek, &wrapped)?;
        Ok(Vault { mdk, keys_file })
    }

    pub fn unlock_with_recovery(
        recovery_key: &str,
        keys_json: &[u8],
    ) -> Result<Vault, CryptoError> {
        let keys_file: KeysFile = serde_json::from_slice(keys_json)?;
        let salt = hex::decode(&keys_file.salt)?;
        let wrapped = hex::decode(&keys_file.wrapped_mdk_rec)?;
        let recovery_bytes = hex::decode(recovery_key)?;
        let rec_kek = derive_kek_from_recovery(&recovery_bytes, &salt);
        let mdk = unwrap_mdk(&rec_kek, &wrapped)?;
        Ok(Vault { mdk, keys_file })
    }

    pub fn encrypt_blob(&self, data: &[u8]) -> Result<Vec<u8>, CryptoError> {
        let key = derive_purpose_key(&self.mdk, b"blob");
        xchacha_encrypt(&key, data)
    }

    pub fn decrypt_blob(&self, data: &[u8]) -> Result<Vec<u8>, CryptoError> {
        let key = derive_purpose_key(&self.mdk, b"blob");
        xchacha_decrypt(&key, data)
    }

    pub fn encrypt_heads(&self, data: &[u8]) -> Result<Vec<u8>, CryptoError> {
        let key = derive_purpose_key(&self.mdk, b"heads");
        xchacha_encrypt(&key, data)
    }

    pub fn decrypt_heads(&self, data: &[u8]) -> Result<Vec<u8>, CryptoError> {
        let key = derive_purpose_key(&self.mdk, b"heads");
        xchacha_decrypt(&key, data)
    }

    pub fn blob_name(&self, hash: &str) -> String {
        hmac_name(&self.mdk, b"blob-name", hash)
    }

    pub fn path_segment(&self, name: &str) -> String {
        hmac_name(&self.mdk, b"path-segment", name)
    }
}

impl Drop for Vault {
    fn drop(&mut self) {
        self.mdk.zeroize();
    }
}

#[cfg(feature = "switch")]
mod switch_entropy {
    use getrandom::register_custom_getrandom;

    fn nx_entropy(buf: &mut [u8]) -> Result<(), getrandom::Error> {
        unsafe extern "C" {
            fn nx_getrandom(buf: *mut u8, len: usize);
        }
        // SAFETY: nx_getrandom is provided by the C++ libnx link step and
        // writes exactly `len` random bytes to `buf`. The pointer and length
        // come from a valid mutable slice.
        unsafe {
            nx_getrandom(buf.as_mut_ptr(), buf.len());
        }
        Ok(())
    }

    register_custom_getrandom!(nx_entropy);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vault_init_produces_valid_keys_json() {
        let (vault, recovery_key) = Vault::init("test-passphrase").unwrap();
        let keys_json = vault.keys_json().unwrap();
        assert!(!recovery_key.is_empty());
        let parsed: KeysFile = serde_json::from_slice(&keys_json).unwrap();
        assert!(!parsed.salt.is_empty());
        assert!(!parsed.wrapped_mdk_pass.is_empty());
        assert!(!parsed.wrapped_mdk_rec.is_empty());
    }

    #[test]
    fn vault_unlock_with_passphrase() {
        let (vault, _recovery_key) = Vault::init("my-password").unwrap();
        let keys_json = vault.keys_json().unwrap();
        let vault2 = Vault::unlock_with_passphrase("my-password", &keys_json).unwrap();
        let plaintext = b"hello world";
        let encrypted = vault.encrypt_blob(plaintext).unwrap();
        let decrypted = vault2.decrypt_blob(&encrypted).unwrap();
        assert_eq!(decrypted, plaintext);
    }

    #[test]
    fn vault_unlock_with_recovery_key() {
        let (vault, recovery_key) = Vault::init("my-password").unwrap();
        let keys_json = vault.keys_json().unwrap();
        let vault2 = Vault::unlock_with_recovery(&recovery_key, &keys_json).unwrap();
        let plaintext = b"secret save data";
        let encrypted = vault.encrypt_blob(plaintext).unwrap();
        let decrypted = vault2.decrypt_blob(&encrypted).unwrap();
        assert_eq!(decrypted, plaintext);
    }

    #[test]
    fn wrong_passphrase_fails() {
        let (vault, _) = Vault::init("correct").unwrap();
        let keys_json = vault.keys_json().unwrap();
        let result = Vault::unlock_with_passphrase("wrong", &keys_json);
        assert!(result.is_err());
    }

    #[test]
    fn blob_encrypt_decrypt_round_trip() {
        let (vault, _) = Vault::init("pw").unwrap();
        let data = vec![0xAB; 1024];
        let encrypted = vault.encrypt_blob(&data).unwrap();
        assert_ne!(encrypted, data);
        let decrypted = vault.decrypt_blob(&encrypted).unwrap();
        assert_eq!(decrypted, data);
    }

    #[test]
    fn heads_encrypt_decrypt_round_trip() {
        let (vault, _) = Vault::init("pw").unwrap();
        let heads = b"{\"hash\":\"abc\",\"mtime\":\"2026-01-01\"}";
        let encrypted = vault.encrypt_heads(heads).unwrap();
        let decrypted = vault.decrypt_heads(&encrypted).unwrap();
        assert_eq!(decrypted, heads);
    }

    #[test]
    fn blob_name_is_deterministic() {
        let (vault, _) = Vault::init("pw").unwrap();
        let name1 = vault.blob_name("abc123hash");
        let name2 = vault.blob_name("abc123hash");
        assert_eq!(name1, name2);
        assert_ne!(name1, "abc123hash");
    }

    #[test]
    fn path_segment_is_deterministic() {
        let (vault, _) = Vault::init("pw").unwrap();
        let seg1 = vault.path_segment("switch");
        let seg2 = vault.path_segment("switch");
        assert_eq!(seg1, seg2);
        assert_ne!(seg1, "switch");
    }

    #[test]
    fn different_inputs_different_blob_names() {
        let (vault, _) = Vault::init("pw").unwrap();
        let name1 = vault.blob_name("hash_a");
        let name2 = vault.blob_name("hash_b");
        assert_ne!(name1, name2);
    }

    #[test]
    fn different_vaults_different_hmacs() {
        let (vault1, _) = Vault::init("pw1").unwrap();
        let (vault2, _) = Vault::init("pw2").unwrap();
        let name1 = vault1.blob_name("same_hash");
        let name2 = vault2.blob_name("same_hash");
        assert_ne!(name1, name2);
    }
}
