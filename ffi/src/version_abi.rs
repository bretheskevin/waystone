use crate::adapter_abi::cstr_arg;
use core::ffi::c_char;
use waystone_core::version::version_newer;

/// NULL or non-UTF-8 arguments compare as "".
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_version_newer(latest: *const c_char, current: *const c_char) -> bool {
    let (latest, current) = unsafe { (cstr_arg(latest), cstr_arg(current)) };
    version_newer(latest.unwrap_or(""), current.unwrap_or(""))
}
