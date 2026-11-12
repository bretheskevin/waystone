use crate::buffer::{WsBuf, optional_str, owned_c_string};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use core::ffi::{CStr, c_char};
use waystone_core::crypto::{BLOB_EXT_ENCRYPTED, KeysMode, Vault, keys_mode};

pub type WsVault = Vault;

pub const WS_KEYS_MODE_PLAIN: i32 = 0;
pub const WS_KEYS_MODE_ENCRYPTED: i32 = 1;

/// Returns WS_KEYS_MODE_PLAIN / WS_KEYS_MODE_ENCRYPTED, or -1 (last error set)
/// on NULL or unrecognized keys.json.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_keys_mode(keys_json: *const u8, len: usize) -> i32 {
    if keys_json.is_null() {
        set_last_error("null pointer argument");
        return -1;
    }
    catch_and_set_error(|| {
        let keys = unsafe { core::slice::from_raw_parts(keys_json, len) };
        Ok(match keys_mode(keys)? {
            KeysMode::Plain => WS_KEYS_MODE_PLAIN,
            KeysMode::Encrypted => WS_KEYS_MODE_ENCRYPTED,
        })
    })
    .unwrap_or(-1)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_plain() -> *mut WsVault {
    Box::into_raw(Box::new(Vault::plain()))
}

/// Opens a vault from keys.json. `secret` may be NULL for an unencrypted vault;
/// it is the passphrase, or the recovery hex when `recovery` is true.
/// Returns NULL with the last error set on failure.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_open(
    keys_json: *const u8,
    len: usize,
    secret: *const c_char,
    recovery: bool,
) -> *mut WsVault {
    if keys_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let keys = unsafe { core::slice::from_raw_parts(keys_json, len) };
        let secret = unsafe { optional_str(secret) }?;
        let vault = Vault::open(keys, secret, recovery)?;
        Ok(Box::into_raw(Box::new(vault)))
    })
    .unwrap_or(core::ptr::null_mut())
}

/// NULL vault -> false.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_is_encrypted(vault: *const WsVault) -> bool {
    !vault.is_null() && unsafe { &*vault }.is_encrypted()
}

/// Blob file extension without the dot ("zip" plain, "bin" encrypted; NULL vault -> "bin").
/// The returned string is static: do not free it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_blob_ext(vault: *const WsVault) -> *const c_char {
    if vault.is_null() {
        return BLOB_EXT_ENCRYPTED.as_ptr();
    }
    unsafe { &*vault }.blob_ext_cstr().as_ptr()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_keys_json(vault: *const WsVault) -> WsBuf {
    if vault.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| Ok(WsBuf::from_vec(unsafe { &*vault }.keys_json()?)))
        .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_init(
    passphrase: *const c_char,
    out_recovery_hex: *mut WsBuf,
    out_keys_json: *mut WsBuf,
) -> *mut WsVault {
    if passphrase.is_null() || out_recovery_hex.is_null() || out_keys_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let passphrase = unsafe { CStr::from_ptr(passphrase) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let (vault, recovery_hex) = Vault::init(passphrase)?;
        let keys_json = vault.keys_json()?;

        unsafe {
            *out_recovery_hex = WsBuf::from_vec(recovery_hex.into_bytes());
            *out_keys_json = WsBuf::from_vec(keys_json);
        }

        Ok(Box::into_raw(Box::new(vault)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_unlock_pass(
    passphrase: *const c_char,
    keys_json: *const u8,
    n: usize,
) -> *mut WsVault {
    if passphrase.is_null() || keys_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let passphrase = unsafe { CStr::from_ptr(passphrase) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let keys = unsafe { core::slice::from_raw_parts(keys_json, n) };
        let vault = Vault::unlock_with_passphrase(passphrase, keys)?;
        Ok(Box::into_raw(Box::new(vault)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_unlock_recovery(
    recovery_hex: *const c_char,
    keys_json: *const u8,
    n: usize,
) -> *mut WsVault {
    if recovery_hex.is_null() || keys_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let recovery = unsafe { CStr::from_ptr(recovery_hex) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let keys = unsafe { core::slice::from_raw_parts(keys_json, n) };
        let vault = Vault::unlock_with_recovery(recovery, keys)?;
        Ok(Box::into_raw(Box::new(vault)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_encrypt_blob(
    vault: *const WsVault,
    data: *const u8,
    len: usize,
) -> WsBuf {
    if vault.is_null() || data.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let data = unsafe { core::slice::from_raw_parts(data, len) };
        let encrypted = vault.encrypt_blob(data)?;
        Ok(WsBuf::from_vec(encrypted))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_decrypt_blob(
    vault: *const WsVault,
    data: *const u8,
    len: usize,
) -> WsBuf {
    if vault.is_null() || data.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let data = unsafe { core::slice::from_raw_parts(data, len) };
        let decrypted = vault.decrypt_blob(data)?;
        Ok(WsBuf::from_vec(decrypted))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_encrypt_heads(
    vault: *const WsVault,
    data: *const u8,
    len: usize,
) -> WsBuf {
    if vault.is_null() || data.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let data = unsafe { core::slice::from_raw_parts(data, len) };
        let encrypted = vault.encrypt_heads(data)?;
        Ok(WsBuf::from_vec(encrypted))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_decrypt_heads(
    vault: *const WsVault,
    data: *const u8,
    len: usize,
) -> WsBuf {
    if vault.is_null() || data.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let data = unsafe { core::slice::from_raw_parts(data, len) };
        let decrypted = vault.decrypt_heads(data)?;
        Ok(WsBuf::from_vec(decrypted))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_blob_name(
    vault: *const WsVault,
    hash: *const c_char,
) -> *mut c_char {
    if vault.is_null() || hash.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let hash = unsafe { CStr::from_ptr(hash) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        Ok(owned_c_string(vault.blob_name(hash)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_path_segment(
    vault: *const WsVault,
    name: *const c_char,
) -> *mut c_char {
    if vault.is_null() || name.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let vault = unsafe { &*vault };
        let name = unsafe { CStr::from_ptr(name) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        Ok(owned_c_string(vault.path_segment(name)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_export_mdk(vault: *const WsVault) -> WsBuf {
    if vault.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    match unsafe { &*vault }.export_mdk() {
        Some(mdk) => WsBuf::from_vec(mdk.to_vec()),
        None => {
            set_last_error("unencrypted vault has no key");
            WsBuf::null()
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_from_mdk(mdk: *const u8, mdk_len: usize) -> *mut WsVault {
    if mdk.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    if mdk_len != 32 {
        set_last_error("MDK must be exactly 32 bytes");
        return core::ptr::null_mut();
    }
    let mdk_slice = unsafe { core::slice::from_raw_parts(mdk, mdk_len) };
    let mut mdk_arr = [0u8; 32];
    mdk_arr.copy_from_slice(mdk_slice);
    let vault = Vault::from_mdk(mdk_arr);
    // Zeroize stack-local MDK copy (write_volatile prevents elision)
    for byte in mdk_arr.iter_mut() {
        unsafe { core::ptr::write_volatile(byte, 0) };
    }
    Box::into_raw(Box::new(vault))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_vault_free(vault: *mut WsVault) {
    if !vault.is_null() {
        unsafe {
            drop(Box::from_raw(vault));
        }
    }
}
