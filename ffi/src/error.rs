use alloc::boxed::Box;
use alloc::ffi::CString;
use alloc::string::{String, ToString};
use core::ffi::c_char;
use spin::Mutex;

static LAST_ERROR: Mutex<Option<CString>> = Mutex::new(None);

pub fn set_last_error(msg: &str) {
    if let Ok(cs) = CString::new(msg) {
        *LAST_ERROR.lock() = Some(cs);
    }
}

/// # Safety
/// The returned pointer is valid until the next call that modifies `LAST_ERROR`
/// (process-global since the `thread_local!` -> `spin::Mutex` migration).
/// Callers must copy the string before any subsequent FFI call that could set an error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_last_error() -> *const c_char {
    let guard = LAST_ERROR.lock();
    match guard.as_ref() {
        Some(cs) => cs.as_ptr(),
        None => core::ptr::null(),
    }
}

pub fn catch_and_set_error<F, T>(f: F) -> Option<T>
where
    F: FnOnce() -> Result<T, Box<dyn core::error::Error>>,
{
    match f() {
        Ok(val) => Some(val),
        Err(e) => {
            set_last_error(&e.to_string());
            None
        }
    }
}

/// A simple string-backed error for wrapping errors that don't implement `core::error::Error`.
#[derive(Debug)]
pub struct StringError(pub String);

impl core::fmt::Display for StringError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(&self.0)
    }
}

impl core::error::Error for StringError {}
