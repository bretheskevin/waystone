use crate::buffer::{WsBuf, owned_c_string};
use crate::dto::NormalizedSaveMeta;
use crate::error::{catch_and_set_error, set_last_error};
use crate::wire::{decode_file_tree, encode_file_tree};
use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};
use waystone_core::packaging;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_canonical_zip(files: *const u8, files_len: usize) -> WsBuf {
    if files.is_null() {
        set_last_error("null files pointer");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let bytes = unsafe { core::slice::from_raw_parts(files, files_len) };
        let decoded: Vec<(String, Vec<u8>)> =
            decode_file_tree(bytes).map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let zip_bytes = packaging::canonical_zip(&decoded);
        Ok(WsBuf::from_vec(zip_bytes))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_unzip(data: *const u8, len: usize) -> WsBuf {
    if data.is_null() {
        set_last_error("null data pointer");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let bytes = unsafe { core::slice::from_raw_parts(data, len) };
        let files = packaging::unzip(bytes)?;
        Ok(WsBuf::from_vec(encode_file_tree(&files)))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_content_hash(data: *const u8, len: usize) -> *mut c_char {
    if data.is_null() {
        set_last_error("null data pointer");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let bytes = unsafe { core::slice::from_raw_parts(data, len) };
        Ok(owned_c_string(packaging::content_hash(bytes)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_file_hash(data: *const u8, len: usize) -> *mut c_char {
    if data.is_null() {
        set_last_error("null data pointer");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let bytes = unsafe { core::slice::from_raw_parts(data, len) };
        Ok(owned_c_string(packaging::file_hash(bytes)))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_package(
    meta_json: *const c_char,
    files: *const u8,
    files_len: usize,
    out_zip: *mut WsBuf,
) -> *mut c_char {
    if meta_json.is_null() || files.is_null() || out_zip.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let json_str = unsafe { CStr::from_ptr(meta_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let meta: NormalizedSaveMeta = serde_json::from_str(json_str)?;
        let bytes = unsafe { core::slice::from_raw_parts(files, files_len) };
        let decoded: Vec<(String, Vec<u8>)> =
            decode_file_tree(bytes).map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let save = meta.into_save(decoded);
        let (entry, zip_bytes) = packaging::package(&save);
        let entry_json = serde_json::to_string(&entry)?;
        unsafe {
            *out_zip = WsBuf::from_vec(zip_bytes);
        }
        Ok(owned_c_string(entry_json))
    })
    .unwrap_or(core::ptr::null_mut())
}
