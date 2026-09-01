use crate::buffer::owned_c_string;
use crate::dto::{NormalizedSaveDto, RawTreeDto};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::ToString;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};
use waystone_core::adapters::Adapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::model::SystemId;

fn parse_system_id(s: &str) -> Result<SystemId, Box<dyn core::error::Error>> {
    serde_json::from_value(serde_json::Value::String(s.to_string()))
        .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_normalize(
    system: *const c_char,
    raw_tree_json: *const c_char,
) -> *mut c_char {
    if system.is_null() || raw_tree_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let system_str = unsafe { CStr::from_ptr(system) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let sys = parse_system_id(system_str)?;
        let json_str = unsafe { CStr::from_ptr(raw_tree_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let dto: RawTreeDto = serde_json::from_str(json_str)?;
        let tree = dto
            .to_core()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let adapter = JksvAdapter::new(sys);
        let saves = adapter.normalize(&tree);
        let dtos: Vec<NormalizedSaveDto> = saves.iter().map(NormalizedSaveDto::from_core).collect();
        let json = serde_json::to_string(&dtos)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_to_native(normalized_save_json: *const c_char) -> *mut c_char {
    if normalized_save_json.is_null() {
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
        let adapter = JksvAdapter::new(save.id.system);
        let tree = adapter.to_native(&save);
        let result = RawTreeDto::from_core(&tree);
        let json = serde_json::to_string(&result)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_normalize(
    system: *const c_char,
    raw_tree_json: *const c_char,
) -> *mut c_char {
    if system.is_null() || raw_tree_json.is_null() {
        set_last_error("null pointer argument");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let system_str = unsafe { CStr::from_ptr(system) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let sys = parse_system_id(system_str)?;
        let json_str = unsafe { CStr::from_ptr(raw_tree_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let dto: RawTreeDto = serde_json::from_str(json_str)?;
        let tree = dto
            .to_core()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let adapter = MgbaAdapter::new(sys);
        let saves = adapter.normalize(&tree);
        let dtos: Vec<NormalizedSaveDto> = saves.iter().map(NormalizedSaveDto::from_core).collect();
        let json = serde_json::to_string(&dtos)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_to_native(normalized_save_json: *const c_char) -> *mut c_char {
    if normalized_save_json.is_null() {
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
        let adapter = MgbaAdapter::new(save.id.system);
        let tree = adapter.to_native(&save);
        let result = RawTreeDto::from_core(&tree);
        let json = serde_json::to_string(&result)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}
