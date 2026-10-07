use crate::buffer::WsBuf;
use crate::dto::NormalizedSaveMeta;
use crate::error::{catch_and_set_error, set_last_error};
use crate::wire::{FileTree, decode_file_tree, encode_file_tree, encode_save_list};
use alloc::boxed::Box;
use alloc::string::ToString;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char};
use waystone_core::adapters::Adapter;
use waystone_core::adapters::checkpoint::CheckpointAdapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::adapters::rom_keyed::{RomKeyedAdapter, rom_keyed_to_native};
use waystone_core::model::{NormalizedSave, RawFile, RawTree, SystemId};

fn parse_system_id(s: &str) -> Result<SystemId, Box<dyn core::error::Error>> {
    serde_json::from_value(serde_json::Value::String(s.to_string()))
        .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)
}

pub(crate) fn parse_system_ptr(ptr: *const c_char) -> Option<SystemId> {
    if ptr.is_null() {
        set_last_error("null pointer argument");
        return None;
    }
    catch_and_set_error(|| {
        // SAFETY: non-null (checked); valid NUL-terminated C string per FFI contract.
        let s = unsafe { CStr::from_ptr(ptr) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        parse_system_id(s)
    })
}

// SAFETY helper: decode a (ptr,len) WsFileTree into core files.
fn decode_tree_arg(ptr: *const u8, len: usize) -> Result<FileTree, Box<dyn core::error::Error>> {
    if ptr.is_null() {
        return Err("null files pointer".into());
    }
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len) };
    decode_file_tree(bytes).map_err(|e| Box::new(e) as Box<dyn core::error::Error>)
}

fn normalize_via<F>(raw_ptr: *const u8, raw_len: usize, f: F) -> WsBuf
where
    F: FnOnce(&RawTree) -> Vec<NormalizedSave>,
{
    catch_and_set_error(|| {
        let files = decode_tree_arg(raw_ptr, raw_len)?;
        let tree = RawTree {
            files: files
                .into_iter()
                .map(|(path, content)| RawFile { path, content })
                .collect(),
        };
        let saves = f(&tree);
        let encoded: Vec<(Vec<u8>, Vec<u8>)> = saves
            .iter()
            .map(|s| {
                let meta = serde_json::to_vec(&NormalizedSaveMeta::from_core(s))?;
                Ok((meta, encode_file_tree(&s.files)))
            })
            .collect::<Result<Vec<_>, Box<dyn core::error::Error>>>()?;
        Ok(WsBuf::from_vec(encode_save_list(&encoded)))
    })
    .unwrap_or_else(WsBuf::null)
}

fn to_native_via<F>(
    meta_json: *const c_char,
    files_ptr: *const u8,
    files_len: usize,
    out_tree: *mut WsBuf,
    f: F,
) -> i32
where
    F: FnOnce(&NormalizedSave) -> RawTree,
{
    if meta_json.is_null() || out_tree.is_null() {
        set_last_error("null pointer argument");
        return -1;
    }
    let result = catch_and_set_error(|| {
        // SAFETY: non-null (checked); valid NUL-terminated C string per FFI contract.
        let json_str = unsafe { CStr::from_ptr(meta_json) }
            .to_str()
            .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)?;
        let meta: NormalizedSaveMeta = serde_json::from_str(json_str)?;
        let files = decode_tree_arg(files_ptr, files_len)?;
        let save = meta.into_save(files);
        let tree = f(&save);
        let pairs: Vec<(alloc::string::String, Vec<u8>)> = tree
            .files
            .into_iter()
            .map(|f| (f.path, f.content))
            .collect();
        Ok(WsBuf::from_vec(encode_file_tree(&pairs)))
    });
    match result {
        Some(buf) => {
            // SAFETY: out_tree non-null (checked above).
            unsafe { *out_tree = buf };
            0
        }
        None => -1,
    }
}

// ── checkpoint ────────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_checkpoint_normalize(
    system: *const c_char,
    raw_files: *const u8,
    raw_files_len: usize,
) -> WsBuf {
    let Some(sys) = parse_system_ptr(system) else {
        return WsBuf::null();
    };
    normalize_via(raw_files, raw_files_len, |t| {
        CheckpointAdapter::new(sys).normalize(t)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_checkpoint_to_native(
    meta_json: *const c_char,
    files: *const u8,
    files_len: usize,
    out_tree: *mut WsBuf,
) -> i32 {
    to_native_via(meta_json, files, files_len, out_tree, |s| {
        CheckpointAdapter::new(s.id.system).to_native(s)
    })
}

// ── jksv ──────────────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_normalize(
    system: *const c_char,
    raw_files: *const u8,
    raw_files_len: usize,
) -> WsBuf {
    let Some(sys) = parse_system_ptr(system) else {
        return WsBuf::null();
    };
    normalize_via(raw_files, raw_files_len, |t| {
        JksvAdapter::new(sys).normalize(t)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_jksv_to_native(
    meta_json: *const c_char,
    files: *const u8,
    files_len: usize,
    out_tree: *mut WsBuf,
) -> i32 {
    to_native_via(meta_json, files, files_len, out_tree, |s| {
        JksvAdapter::new(s.id.system).to_native(s)
    })
}

// ── mgba ────────────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_normalize(
    system: *const c_char,
    raw_files: *const u8,
    raw_files_len: usize,
) -> WsBuf {
    let Some(sys) = parse_system_ptr(system) else {
        return WsBuf::null();
    };
    normalize_via(raw_files, raw_files_len, |t| {
        MgbaAdapter::new(sys).normalize(t)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_mgba_to_native(
    meta_json: *const c_char,
    files: *const u8,
    files_len: usize,
    out_tree: *mut WsBuf,
) -> i32 {
    to_native_via(meta_json, files, files_len, out_tree, |s| {
        MgbaAdapter::new(s.id.system).to_native(s)
    })
}

pub(crate) unsafe fn cstr_arg<'a>(
    ptr: *const c_char,
) -> Result<&'a str, Box<dyn core::error::Error>> {
    if ptr.is_null() {
        return Err("null pointer argument".into());
    }
    // SAFETY: non-null (checked); valid NUL-terminated C string per FFI contract.
    unsafe { CStr::from_ptr(ptr) }
        .to_str()
        .map_err(|e| Box::new(e) as Box<dyn core::error::Error>)
}

// ── rom_keyed ─────────────────────────────────────────────────────────────────

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_keyed_normalize(
    system: *const c_char,
    rom_id: *const c_char,
    display_name: *const c_char,
    rom_file_name: *const c_char,
    raw_files: *const u8,
    raw_files_len: usize,
) -> WsBuf {
    let Some(sys) = parse_system_ptr(system) else {
        return WsBuf::null();
    };
    let Some((id, name, rom)) = catch_and_set_error(|| unsafe {
        Ok((
            cstr_arg(rom_id)?,
            cstr_arg(display_name)?,
            cstr_arg(rom_file_name)?,
        ))
    }) else {
        return WsBuf::null();
    };
    normalize_via(raw_files, raw_files_len, |t| {
        RomKeyedAdapter::new(sys, id, name, rom).normalize(t)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn ws_rom_keyed_to_native(
    system: *const c_char,
    slot: *const c_char,
    rom_file_name: *const c_char,
    files: *const u8,
    files_len: usize,
    out_tree: *mut WsBuf,
) -> i32 {
    if out_tree.is_null() {
        set_last_error("null pointer argument");
        return -1;
    }
    let Some(sys) = parse_system_ptr(system) else {
        return -1;
    };
    let result = catch_and_set_error(|| {
        let slot = unsafe { cstr_arg(slot)? };
        let rom = unsafe { cstr_arg(rom_file_name)? };
        let decoded = decode_tree_arg(files, files_len)?;
        let native = rom_keyed_to_native(sys, slot, rom, &decoded);
        if native.is_empty() && !decoded.is_empty() {
            return Err("no blob file maps to the requested slot".into());
        }
        Ok(WsBuf::from_vec(encode_file_tree(&native)))
    });
    match result {
        Some(buf) => {
            // SAFETY: out_tree non-null (checked above).
            unsafe { *out_tree = buf };
            0
        }
        None => -1,
    }
}
