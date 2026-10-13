use crate::adapter_abi::{cstr_arg, parse_system_ptr};
use crate::buffer::{WsBuf, owned_c_string};
use crate::error::{catch_and_set_error, set_last_error};
use alloc::boxed::Box;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{c_char, c_void};
use waystone_core::crc32::crc32_update;
use waystone_core::iso9660::{cue_first_data_file, psx_boot_serial, psx_fallback_id};
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

/// Random-access reader for PSX images: read up to `len` bytes at `offset` into
/// `buf`, return bytes actually read (0 = EOF/error). `user` is the shell's ctx.
/// NULL when the shell cannot serve the image (identity falls back to sampled).
pub type WsReadAtFn =
    Option<unsafe extern "C" fn(user: *mut c_void, offset: u64, buf: *mut u8, len: usize) -> usize>;

/// Parse a .cue sheet, return the first data-track FILE name. Caller frees with
/// ws_string_free. NULL on parse failure (ws_last_error set).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_psx_cue_data_file(
    cue_text: *const u8,
    cue_text_len: usize,
) -> *mut c_char {
    if cue_text.is_null() || cue_text_len == 0 {
        set_last_error("empty cue sheet");
        return core::ptr::null_mut();
    }
    let text = match core::str::from_utf8(unsafe { bytes_arg(cue_text, cue_text_len) }) {
        Ok(t) => t,
        Err(_) => {
            set_last_error("cue sheet is not UTF-8");
            return core::ptr::null_mut();
        }
    };
    match cue_first_data_file(text) {
        Some(name) => owned_c_string(name),
        None => {
            set_last_error("cue sheet has no FILE line");
            core::ptr::null_mut()
        }
    }
}

/// PSX identity: ISO9660 SYSTEM.CNF boot serial when `read_cb` can serve a
/// parseable image; otherwise the sampled fallback `size-crc32`. Caller frees
/// with ws_string_free. Never returns NULL.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_psx_identity(
    read_cb: WsReadAtFn,
    user: *mut c_void,
    file_size: u64,
    sample_crc32: u32,
) -> *mut c_char {
    if let Some(cb) = read_cb {
        let mut read = |off: u64, buf: &mut [u8]| -> usize {
            // SAFETY: the shell guarantees `user`/`buf` valid for the call duration.
            unsafe { cb(user, off, buf.as_mut_ptr(), buf.len()) }
        };
        if let Some(serial) = psx_boot_serial(&mut read) {
            return owned_c_string(serial);
        }
    }
    owned_c_string(psx_fallback_id(file_size, sample_crc32))
}
