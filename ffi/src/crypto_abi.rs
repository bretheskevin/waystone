use crate::buffer::{WsBuf, owned_c_string};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use core::ffi::{CStr, c_char};
use waystone_core::crypto::Vault;

pub type WsVault = Vault;

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
    let vault = unsafe { &*vault };
    let mdk = vault.export_mdk();
    WsBuf::from_vec(mdk.to_vec())
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
