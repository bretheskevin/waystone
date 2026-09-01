use std::ffi::CString;

#[repr(C)]
pub struct WsBuf {
    pub ptr: *mut u8,
    pub len: usize,
}

impl WsBuf {
    pub fn from_vec(v: Vec<u8>) -> Self {
        let mut boxed = v.into_boxed_slice();
        let buf = WsBuf {
            ptr: boxed.as_mut_ptr(),
            len: boxed.len(),
        };
        std::mem::forget(boxed);
        buf
    }

    pub fn null() -> Self {
        WsBuf {
            ptr: std::ptr::null_mut(),
            len: 0,
        }
    }

    /// # Safety
    /// `ptr` must point to `len` bytes that remain valid for the lifetime of `&self`.
    pub unsafe fn as_slice(&self) -> &[u8] {
        if self.ptr.is_null() || self.len == 0 {
            &[]
        } else {
            unsafe { std::slice::from_raw_parts(self.ptr, self.len) }
        }
    }
}

pub fn owned_c_string(s: String) -> *mut std::ffi::c_char {
    match CString::new(s) {
        Ok(cs) => cs.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}

/// # Safety
/// `buf.ptr` must have been allocated by `WsBuf::from_vec` or be null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_buf_free(buf: WsBuf) {
    if !buf.ptr.is_null() && buf.len > 0 {
        unsafe {
            drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                buf.ptr, buf.len,
            )));
        }
    }
}

/// # Safety
/// `s` must have been returned by `owned_c_string` or be null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_string_free(s: *mut std::ffi::c_char) {
    if !s.is_null() {
        unsafe {
            drop(CString::from_raw(s));
        }
    }
}
