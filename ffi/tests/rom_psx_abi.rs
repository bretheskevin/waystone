use std::ffi::{CStr, c_void};

// Force the crate into the link: the extern block alone doesn't mark it used.
extern crate waystone_ffi;

unsafe extern "C" {
    fn ws_rom_psx_cue_data_file(cue_text: *const u8, cue_text_len: usize) -> *mut std::ffi::c_char;
    fn ws_rom_psx_identity(
        read_cb: Option<unsafe extern "C" fn(*mut c_void, u64, *mut u8, usize) -> usize>,
        user: *mut c_void,
        file_size: u64,
        sample_crc32: u32,
    ) -> *mut std::ffi::c_char;
    fn ws_string_free(s: *mut std::ffi::c_char);
}

#[test]
fn cue_data_file_parse() {
    let cue = b"FILE \"Crash Bandicoot.bin\" BINARY\n  TRACK 01 MODE2/2352\n";
    let p = unsafe { ws_rom_psx_cue_data_file(cue.as_ptr(), cue.len()) };
    assert!(!p.is_null());
    let s = unsafe { CStr::from_ptr(p) }.to_str().unwrap().to_owned();
    unsafe { ws_string_free(p) };
    assert_eq!(s, "Crash Bandicoot.bin");
}

#[test]
fn identity_without_reader_is_sampled_fallback() {
    let p = unsafe { ws_rom_psx_identity(None, std::ptr::null_mut(), 0x260000000, 0xCBF43926) };
    assert!(!p.is_null());
    let s = unsafe { CStr::from_ptr(p) }.to_str().unwrap().to_owned();
    unsafe { ws_string_free(p) };
    assert_eq!(s, "260000000-CBF43926");
}

#[test]
fn identity_with_garbage_reader_falls_back() {
    unsafe extern "C" fn eof(_: *mut c_void, _: u64, _: *mut u8, _: usize) -> usize {
        0
    }
    let p = unsafe { ws_rom_psx_identity(Some(eof), std::ptr::null_mut(), 100, 0xDEADBEEF) };
    assert!(!p.is_null());
    let s = unsafe { CStr::from_ptr(p) }.to_str().unwrap().to_owned();
    unsafe { ws_string_free(p) };
    assert_eq!(s, "64-DEADBEEF"); // 100 = 0x64
}
