use crate::adapter_abi::cstr_arg;
use crate::buffer::owned_c_string;
use core::ffi::c_char;
use waystone_core::url::redact_userinfo;

/// `url` with any `user:pass@` replaced by `***@`, for log lines; free with `ws_string_free`.
/// NULL or non-UTF-8 input returns NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_redact_url(url: *const c_char) -> *mut c_char {
    match unsafe { cstr_arg(url) } {
        Ok(u) => owned_c_string(redact_userinfo(u)),
        Err(_) => core::ptr::null_mut(),
    }
}
