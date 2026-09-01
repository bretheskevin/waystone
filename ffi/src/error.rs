use std::cell::RefCell;
use std::ffi::{CString, c_char};

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

pub fn set_last_error(msg: &str) {
    LAST_ERROR.with(|cell| {
        *cell.borrow_mut() = CString::new(msg).ok();
    });
}

/// # Safety
/// The returned pointer is valid until the next call that modifies `LAST_ERROR` on this thread.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_last_error() -> *const c_char {
    LAST_ERROR.with(|cell| match cell.borrow().as_ref() {
        Some(cs) => cs.as_ptr(),
        None => std::ptr::null(),
    })
}

pub fn catch_and_set_error<F, T>(f: F) -> Option<T>
where
    F: FnOnce() -> Result<T, Box<dyn std::error::Error>> + std::panic::UnwindSafe,
{
    match std::panic::catch_unwind(f) {
        Ok(Ok(val)) => Some(val),
        Ok(Err(e)) => {
            set_last_error(&e.to_string());
            None
        }
        Err(_) => {
            set_last_error("panic in FFI boundary");
            None
        }
    }
}
