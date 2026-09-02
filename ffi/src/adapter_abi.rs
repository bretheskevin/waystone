use crate::buffer::owned_c_string;
use crate::dto::{NormalizedSaveDto, RawTreeDto};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::ToString;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};
use waystone_core::adapters::Adapter;
use waystone_core::adapters::checkpoint::CheckpointAdapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::adapters::twilight::TwilightAdapter;
use waystone_core::model::{NormalizedSave, RawTree, SystemId};

fn parse_system_id(s: &str) -> Result<SystemId, Box<dyn core::error::Error>> {
    serde_json::from_value(serde_json::Value::String(s.to_string()))
        .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)
}

fn parse_system_ptr(ptr: *const c_char) -> Option<SystemId> {
    if ptr.is_null() {
        set_last_error("null pointer argument");
        return None;
    }
    catch_and_set_error(|| {
        // SAFETY: `ptr` is non-null (checked above) and must point to a
        // valid NUL-terminated C string per the FFI contract.
        let s = unsafe { CStr::from_ptr(ptr) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        parse_system_id(s)
    })
}

fn normalize_via<F>(raw_tree_json: *const c_char, f: F) -> *mut c_char
where
    F: FnOnce(&RawTree) -> Vec<NormalizedSave>,
{
    if raw_tree_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        // SAFETY: `raw_tree_json` is non-null (checked above) and must point
        // to a valid NUL-terminated C string per the FFI contract.
        let json_str = unsafe { CStr::from_ptr(raw_tree_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let dto: RawTreeDto = serde_json::from_str(json_str)?;
        let tree = dto
            .to_core()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let saves = f(&tree);
        let dtos: Vec<NormalizedSaveDto> = saves.iter().map(NormalizedSaveDto::from_core).collect();
        let json = serde_json::to_string(&dtos)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

fn to_native_via<F>(save_json: *const c_char, f: F) -> *mut c_char
where
    F: FnOnce(&NormalizedSave) -> RawTree,
{
    if save_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        // SAFETY: `save_json` is non-null (checked above) and must point to
        // a valid NUL-terminated C string per the FFI contract.
        let json_str = unsafe { CStr::from_ptr(save_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let dto: NormalizedSaveDto = serde_json::from_str(json_str)?;
        let save = dto
            .to_core()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let tree = f(&save);
        let result = RawTreeDto::from_core(&tree);
        let json = serde_json::to_string(&result)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_checkpoint_normalize(
    system: *const c_char,
    raw_tree_json: *const c_char,
) -> *mut c_char {
    let Some(sys) = parse_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    normalize_via(raw_tree_json, |t| CheckpointAdapter::new(sys).normalize(t))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_checkpoint_to_native(
    normalized_save_json: *const c_char,
) -> *mut c_char {
    to_native_via(normalized_save_json, |s| {
        CheckpointAdapter::new(s.id.system).to_native(s)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_normalize(
    system: *const c_char,
    raw_tree_json: *const c_char,
) -> *mut c_char {
    let Some(sys) = parse_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    normalize_via(raw_tree_json, |t| JksvAdapter::new(sys).normalize(t))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_to_native(normalized_save_json: *const c_char) -> *mut c_char {
    to_native_via(normalized_save_json, |s| {
        JksvAdapter::new(s.id.system).to_native(s)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_normalize(
    system: *const c_char,
    raw_tree_json: *const c_char,
) -> *mut c_char {
    let Some(sys) = parse_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    normalize_via(raw_tree_json, |t| MgbaAdapter::new(sys).normalize(t))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_to_native(normalized_save_json: *const c_char) -> *mut c_char {
    to_native_via(normalized_save_json, |s| {
        MgbaAdapter::new(s.id.system).to_native(s)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_twilight_normalize(raw_tree_json: *const c_char) -> *mut c_char {
    normalize_via(raw_tree_json, |t| TwilightAdapter::new().normalize(t))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_twilight_to_native(normalized_save_json: *const c_char) -> *mut c_char {
    to_native_via(normalized_save_json, |s| {
        TwilightAdapter::new().to_native(s)
    })
}
