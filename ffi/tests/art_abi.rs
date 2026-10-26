use std::ffi::{CStr, CString};
use waystone_ffi::adapter_abi::*;
use waystone_ffi::buffer::{WsBuf, ws_buf_free, ws_string_free};
use waystone_ffi::rom_abi::*;
use waystone_ffi::url_abi::ws_redact_url;
use waystone_ffi::version_abi::ws_version_newer;
use waystone_ffi::wire::{decode_file_tree, decode_save_list, encode_file_tree};

fn c(s: &str) -> CString {
    CString::new(s).unwrap()
}

unsafe fn take_buf(buf: WsBuf) -> Vec<u8> {
    let v = unsafe { std::slice::from_raw_parts(buf.ptr, buf.len) }.to_vec();
    unsafe { ws_buf_free(buf) };
    v
}

unsafe fn take_str(p: *mut std::ffi::c_char) -> String {
    let s = unsafe { CStr::from_ptr(p) }.to_str().unwrap().to_string();
    unsafe { ws_string_free(p) };
    s
}

#[test]
fn version_newer_over_c_abi() {
    unsafe {
        assert!(ws_version_newer(c("1.10.0").as_ptr(), c("1.9.0").as_ptr()));
        assert!(!ws_version_newer(c("1.0").as_ptr(), c("1.0.0").as_ptr()));
        assert!(!ws_version_newer(std::ptr::null(), std::ptr::null()));
    }
}

#[test]
fn redact_url_over_c_abi() {
    unsafe {
        let out = ws_redact_url(c("http://bob:pw@host:5000/dav").as_ptr());
        assert_eq!(
            CStr::from_ptr(out).to_str().unwrap(),
            "http://***@host:5000/dav"
        );
        ws_string_free(out);
        assert!(ws_redact_url(std::ptr::null()).is_null());
    }
}

#[test]
fn banner_offset_icon_title() {
    let mut header = vec![0u8; 0x200];
    header[0x68..0x6C].copy_from_slice(&0x4000u32.to_le_bytes());
    let mut banner = vec![0u8; 0x840];
    banner[0x222..0x224].copy_from_slice(&0x001Fu16.to_le_bytes());
    banner[0x20] = 0x01;
    for (k, u) in "Game\nPub".encode_utf16().enumerate() {
        banner[0x340 + 2 * k..0x342 + 2 * k].copy_from_slice(&u.to_le_bytes());
    }
    unsafe {
        assert_eq!(ws_nds_banner_offset(header.as_ptr(), header.len()), 0x4000);
        let icon = ws_nds_banner_icon_rgba(banner.as_ptr(), banner.len());
        assert_eq!(icon.len, 4096);
        assert_eq!(std::slice::from_raw_parts(icon.ptr, 4), &[255, 0, 0, 255]);
        ws_buf_free(icon);
        assert!(
            ws_nds_banner_icon_rgba(banner.as_ptr(), 0x100)
                .ptr
                .is_null()
        );
        assert_eq!(
            take_str(ws_nds_banner_title(banner.as_ptr(), banner.len())),
            "Game"
        );
    }
}

#[test]
fn boxart_paths() {
    unsafe {
        assert_eq!(
            take_str(ws_twilight_boxart_paths(c("Zelda.gba").as_ptr())),
            "_nds/TWiLightMenu/boxart/Zelda.gba.bmp\n_nds/TWiLightMenu/boxart/Zelda.gba.png"
        );
        assert_eq!(
            take_str(ws_retroarch_boxart_path(
                c("gba").as_ptr(),
                c("Zelda.gba").as_ptr()
            )),
            "thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Zelda.png"
        );
        assert!(ws_retroarch_boxart_path(c("switch").as_ptr(), c("x").as_ptr()).is_null());
    }
}

#[test]
fn flavored_pairing_maps_external_saves() {
    let list = "retroarch/roms/gba/Zelda.gba\n@saves/Zelda.srm\n";
    unsafe {
        let tsv = take_buf(ws_rom_pair_flavored(
            list.as_ptr(),
            list.len(),
            c("retroarch").as_ptr(),
        ));
        let tsv = String::from_utf8(tsv).unwrap();
        assert!(tsv.contains("\t@saves/Zelda.srm"), "{tsv}");
        let native = take_buf(ws_rom_pair_flavored(
            list.as_ptr(),
            list.len(),
            std::ptr::null(),
        ));
        assert!(
            !String::from_utf8(native)
                .unwrap()
                .contains("@saves/Zelda.srm")
        );
        assert!(
            ws_rom_pair_flavored(list.as_ptr(), list.len(), c("bogus").as_ptr())
                .ptr
                .is_null()
        );
    }
}

#[test]
fn flavored_normalize_and_to_native_round_trip() {
    let raw = encode_file_tree(&[("Zelda.srm".to_string(), vec![7u8, 8])]);
    let (sys, id, dn, rom, fl) = (
        c("gba"),
        c("GBA-1"),
        c("Zelda"),
        c("Zelda.gba"),
        c("retroarch"),
    );
    unsafe {
        let buf = ws_rom_keyed_normalize_flavored(
            sys.as_ptr(),
            id.as_ptr(),
            dn.as_ptr(),
            rom.as_ptr(),
            fl.as_ptr(),
            raw.as_ptr(),
            raw.len(),
        );
        let saves = decode_save_list(&take_buf(buf)).unwrap();
        assert_eq!(saves.len(), 1);
        assert_eq!(saves[0].1[0].0, "battery");
        let files = encode_file_tree(&saves[0].1);
        let slot = c("battery");
        let mut out = WsBuf::null();
        let rc = ws_rom_keyed_to_native_flavored(
            sys.as_ptr(),
            slot.as_ptr(),
            rom.as_ptr(),
            fl.as_ptr(),
            files.as_ptr(),
            files.len(),
            std::ptr::null(),
            0,
            &mut out,
        );
        assert_eq!(rc, 0);
        let tree = decode_file_tree(&take_buf(out)).unwrap();
        assert_eq!(tree, vec![("Zelda.srm".to_string(), vec![7u8, 8])]);
    }
}
