use crate::buffer::owned_c_string;
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::ffi::{CStr, c_char, c_int};
use waystone_core::conflict::{self, ConflictPolicy, DeviceHead};

fn policy_from_int(v: c_int) -> ConflictPolicy {
    match v {
        1 => ConflictPolicy::Prompt,
        _ => ConflictPolicy::NewestWins,
    }
}

unsafe fn optional_str(ptr: *const c_char) -> Result<Option<String>, Box<dyn core::error::Error>> {
    if ptr.is_null() {
        Ok(None)
    } else {
        let s = unsafe { CStr::from_ptr(ptr) }.to_str()?;
        Ok(Some(s.to_string()))
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_fold_heads(heads_json: *const c_char) -> *mut c_char {
    if heads_json.is_null() {
        set_last_error("null heads_json pointer");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let json_str = unsafe { CStr::from_ptr(heads_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let heads: Vec<DeviceHead> = serde_json::from_str(json_str)?;
        match conflict::fold_heads(&heads) {
            Some(merged) => {
                let json = serde_json::to_string(&merged)?;
                Ok(owned_c_string(json))
            }
            None => Ok(core::ptr::null_mut()),
        }
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_three_way_sync(
    local_hash: *const c_char,
    base_hash: *const c_char,
    head_hash: *const c_char,
    local_mtime: *const c_char,
    head_mtime: *const c_char,
    policy: c_int,
) -> *mut c_char {
    if local_mtime.is_null() || head_mtime.is_null() {
        set_last_error("local_mtime and head_mtime must not be null");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let local = unsafe { optional_str(local_hash) }?;
        let base = unsafe { optional_str(base_hash) }?;
        let head = unsafe { optional_str(head_hash) }?;
        let local_mtime_str = unsafe { CStr::from_ptr(local_mtime) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let head_mtime_str = unsafe { CStr::from_ptr(head_mtime) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let decision = conflict::three_way_sync(
            local.as_deref(),
            base.as_deref(),
            head.as_deref(),
            local_mtime_str,
            head_mtime_str,
            policy_from_int(policy),
        );
        let json = serde_json::to_string(&decision)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_decide_pull(
    local_hash: *const c_char,
    local_mtime: *const c_char,
    all_heads_json: *const c_char,
    this_device_id: *const c_char,
    policy: c_int,
) -> *mut c_char {
    if all_heads_json.is_null() || this_device_id.is_null() || local_mtime.is_null() {
        set_last_error("null required pointer");
        return core::ptr::null_mut();
    }
    catch_and_set_error(|| {
        let local = unsafe { optional_str(local_hash) }?;
        let local_mtime_str = unsafe { CStr::from_ptr(local_mtime) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let heads_str = unsafe { CStr::from_ptr(all_heads_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let device_str = unsafe { CStr::from_ptr(this_device_id) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let heads: Vec<DeviceHead> = serde_json::from_str(heads_str)?;
        let decision = conflict::decide_pull(
            local.as_deref(),
            local_mtime_str,
            &heads,
            device_str,
            policy_from_int(policy),
        );
        let json = serde_json::to_string(&decision)?;
        Ok(owned_c_string(json))
    })
    .unwrap_or(core::ptr::null_mut())
}
