use crate::buffer::{WsBuf, owned_c_string};
use crate::dto::{FileEntryDto, NormalizedSaveDto};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};
use waystone_core::packaging;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_canonical_zip(files_json: *const c_char) -> WsBuf {
    if files_json.is_null() {
        set_last_error("null files_json pointer");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let json_str = unsafe { CStr::from_ptr(files_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let entries: Vec<FileEntryDto> = serde_json::from_str(json_str)?;
        let files: Vec<(String, Vec<u8>)> = entries
            .iter()
            .map(|e| Ok((e.path.clone(), e.decode_data()?)))
            .collect::<Result<Vec<_>, Box<dyn core::error::Error>>>()?;
        let zip_bytes = packaging::canonical_zip(&files);
        Ok(WsBuf::from_vec(zip_bytes))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_unzip(data: *const u8, len: usize) -> *mut c_char {
    if data.is_null() {
        set_last_error("null data pointer");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let bytes = unsafe { core::slice::from_raw_parts(data, len) };
        let files = packaging::unzip(bytes)?;
        let dtos: Vec<FileEntryDto> = files
            .iter()
            .map(|(path, content)| FileEntryDto::from_parts(path, content))
            .collect();
        let json = serde_json::to_string(&dtos)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
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
    normalized_save_json: *const c_char,
    out_zip: *mut WsBuf,
) -> *mut c_char {
    if normalized_save_json.is_null() || out_zip.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let json_str = unsafe { CStr::from_ptr(normalized_save_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let dto: NormalizedSaveDto = serde_json::from_str(json_str)?;
        let save = dto
            .to_core()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let (entry, zip_bytes) = packaging::package(&save);
        let entry_json = serde_json::to_string(&entry)?;
        unsafe {
            *out_zip = WsBuf::from_vec(zip_bytes);
        }
        Ok(owned_c_string(entry_json))
    })
    .unwrap_or(core::ptr::null_mut())
}
