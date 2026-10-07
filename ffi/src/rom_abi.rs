use crate::adapter_abi::{cstr_arg, parse_system_ptr};
use crate::buffer::{WsBuf, owned_c_string};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_char;
use waystone_core::crc32::crc32_update;
use waystone_core::model::SystemId;
use waystone_core::rom_id::{display_name, needs_full_hash, rom_identity};
use waystone_core::rom_pair::pair_roms;
use waystone_core::rom_systems::{rom_system, slot_names};

unsafe fn bytes_arg<'a>(ptr: *const u8, len: usize) -> &'a [u8] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        // SAFETY: caller guarantees `len` readable bytes at `ptr`.
        unsafe { core::slice::from_raw_parts(ptr, len) }
    }
}

fn rom_system_ptr(system: *const c_char) -> Option<SystemId> {
    let sys = parse_system_ptr(system)?;
    if rom_system(sys).is_none() {
        set_last_error("system has no ROM support");
        return None;
    }
    Some(sys)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_crc32_update(crc: u32, data: *const u8, len: usize) -> u32 {
    crc32_update(crc, unsafe { bytes_arg(data, len) })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_needs_full_hash(
    system: *const c_char,
    header: *const u8,
    header_len: usize,
) -> i32 {
    let Some(sys) = rom_system_ptr(system) else {
        return -1;
    };
    needs_full_hash(sys, unsafe { bytes_arg(header, header_len) }) as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_identity(
    system: *const c_char,
    header: *const u8,
    header_len: usize,
    full_crc32: u32,
    has_full_crc32: bool,
) -> *mut c_char {
    let Some(sys) = rom_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    let crc = if has_full_crc32 {
        Some(full_crc32)
    } else {
        None
    };
    match rom_identity(sys, unsafe { bytes_arg(header, header_len) }, crc) {
        Some(id) => owned_c_string(id),
        None => {
            set_last_error("full-file CRC32 required for this ROM");
            core::ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_display_name(
    system: *const c_char,
    header: *const u8,
    header_len: usize,
    rom_file_name: *const c_char,
) -> *mut c_char {
    let Some(sys) = rom_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    catch_and_set_error(|| {
        let name = unsafe { cstr_arg(rom_file_name)? };
        Ok(owned_c_string(display_name(
            sys,
            unsafe { bytes_arg(header, header_len) },
            name,
        )))
    })
    .unwrap_or(core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_pair(paths: *const u8, paths_len: usize) -> WsBuf {
    if paths.is_null() {
        set_last_error("null pointer argument");
        return WsBuf::null();
    }
    catch_and_set_error(|| {
        let text = core::str::from_utf8(unsafe { bytes_arg(paths, paths_len) })
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let list: Vec<String> = text
            .split('\n')
            .map(|l| l.trim_end_matches('\r'))
            .filter(|l| !l.is_empty())
            .map(String::from)
            .collect();
        let mut out = String::new();
        for p in pair_roms(&list) {
            out.push_str(p.system.as_str());
            out.push('\t');
            out.push_str(&p.rom_path);
            out.push('\t');
            out.push_str(&p.save_dir);
            for s in &p.save_paths {
                out.push('\t');
                out.push_str(s);
            }
            out.push('\n');
        }
        Ok(WsBuf::from_vec(out.into_bytes()))
    })
    .unwrap_or_else(WsBuf::null)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_keyed_slots(system: *const c_char) -> *mut c_char {
    let Some(sys) = rom_system_ptr(system) else {
        return core::ptr::null_mut();
    };
    owned_c_string(slot_names(sys).join("\n"))
}
