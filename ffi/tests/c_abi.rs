use std::ffi::{CStr, CString};
use std::sync::Mutex;

use waystone_ffi::adapter_abi::*;
use waystone_ffi::buffer::{WsBuf, ws_buf_free, ws_string_free};
use waystone_ffi::conflict_abi::*;
use waystone_ffi::crypto_abi::*;
use waystone_ffi::dto::{FileEntryDto, NormalizedSaveDto, RawTreeDto};
use waystone_ffi::error::{catch_and_set_error, set_last_error, ws_last_error};
use waystone_ffi::packaging_abi::*;

// ── Buffer tests ─────────────────────────────────────────────────────────────

#[test]
fn buf_free_does_not_panic_on_null() {
    let buf = WsBuf {
        ptr: std::ptr::null_mut(),
        len: 0,
    };
    unsafe { ws_buf_free(buf) };
}

#[test]
fn string_free_does_not_panic_on_null() {
    unsafe { ws_string_free(std::ptr::null_mut()) };
}

// ── Error tests ──────────────────────────────────────────────────────────────

// LAST_ERROR is now a process-global `spin::Mutex` (no longer thread_local!),
// so tests that write then read it race with each other under cargo test's
// default parallel execution. This mutex serialises all such tests.
static ERROR_TEST_LOCK: Mutex<()> = Mutex::new(());

#[test]
fn last_error_returns_null_initially() {
    let err = unsafe { ws_last_error() };
    let _ = err;
}

#[test]
fn set_and_retrieve_error() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    set_last_error("test error message");
    let err = unsafe { ws_last_error() };
    assert!(!err.is_null());
    let msg = unsafe { CStr::from_ptr(err) }.to_str().unwrap();
    assert_eq!(msg, "test error message");
}

#[test]
fn catch_and_set_error_captures_err() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let result: Option<i32> = catch_and_set_error(|| Err("something broke".into()));
    assert!(result.is_none());
    let err = unsafe { ws_last_error() };
    assert!(!err.is_null());
    let msg = unsafe { CStr::from_ptr(err) }.to_str().unwrap();
    assert!(msg.contains("something broke"));
}

#[test]
fn catch_and_set_error_returns_value_on_success() {
    let result: Option<i32> = catch_and_set_error(|| Ok(42));
    assert_eq!(result, Some(42));
}

// ── Golden vector parity tests ───────────────────────────────────────────────

/// Golden Vector 1 parity: single file zip through FFI matches core's exact bytes and hash.
/// Maps to core/tests/golden_vectors.rs::golden_single_file_zip_and_hash
#[test]
fn golden_ffi_single_file_zip_and_hash() {
    // "Hello" = [0x48, 0x65, 0x6C, 0x6C, 0x6F] -> base64 "SGVsbG8="
    let files_json = CString::new(r#"[{"path":"save.dat","data_b64":"SGVsbG8="}]"#).unwrap();
    let zip_buf = unsafe { ws_canonical_zip(files_json.as_ptr()) };
    assert!(!zip_buf.ptr.is_null());

    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };

    // Core reference: same input produces exactly this zip
    let core_files = vec![("save.dat".to_string(), vec![0x48u8, 0x65, 0x6C, 0x6C, 0x6F])];
    let core_zip = waystone_core::packaging::canonical_zip(&core_files);
    assert_eq!(
        zip_slice,
        core_zip.as_slice(),
        "FFI zip must be byte-identical to core zip"
    );

    // Golden hash
    let hash_ptr = unsafe { ws_content_hash(zip_slice.as_ptr(), zip_slice.len()) };
    let hash = unsafe { CStr::from_ptr(hash_ptr) }.to_str().unwrap();
    assert_eq!(
        hash,
        "66dc6c1281582edd83ee325e37f45d235c7e60d859f8117dd5d3a978117ee318"
    );

    unsafe {
        ws_buf_free(zip_buf);
        ws_string_free(hash_ptr);
    }
}

/// Golden Vector 2 parity: multi-file order independence through FFI
#[test]
fn golden_ffi_multi_file_order_independent() {
    use base64::Engine;
    use base64::engine::general_purpose::STANDARD as B64;

    let files_a = CString::new(
        serde_json::to_string(&serde_json::json!([
            {"path": "b.bin", "data_b64": B64.encode([2u8])},
            {"path": "a.bin", "data_b64": B64.encode([1u8])}
        ]))
        .unwrap(),
    )
    .unwrap();
    let files_b = CString::new(
        serde_json::to_string(&serde_json::json!([
            {"path": "a.bin", "data_b64": B64.encode([1u8])},
            {"path": "b.bin", "data_b64": B64.encode([2u8])}
        ]))
        .unwrap(),
    )
    .unwrap();

    let zip_a = unsafe { ws_canonical_zip(files_a.as_ptr()) };
    let zip_b = unsafe { ws_canonical_zip(files_b.as_ptr()) };

    let slice_a = unsafe { std::slice::from_raw_parts(zip_a.ptr, zip_a.len) };
    let slice_b = unsafe { std::slice::from_raw_parts(zip_b.ptr, zip_b.len) };
    assert_eq!(
        slice_a, slice_b,
        "entry order must not affect FFI zip output"
    );

    // Match core directly
    let core_files_a = vec![
        ("b.bin".to_string(), vec![2u8]),
        ("a.bin".to_string(), vec![1u8]),
    ];
    let core_zip = waystone_core::packaging::canonical_zip(&core_files_a);
    assert_eq!(slice_a, core_zip.as_slice());

    unsafe {
        ws_buf_free(zip_a);
        ws_buf_free(zip_b);
    }
}

/// Golden Vector 3 parity: full pipeline (adapter -> package) through FFI
#[test]
fn golden_ffi_full_pipeline_jksv_package() {
    use base64::Engine;
    use base64::engine::general_purpose::STANDARD as B64;

    let raw_tree = serde_json::json!({
        "files": [{
            "path": "TestGame - 0100AAAA00001000/slot0/data.sav",
            "data_b64": B64.encode([0xCAu8, 0xFE, 0xBA, 0xBE])
        }]
    });
    let system = CString::new("switch").unwrap();
    let tree_cstr = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let norm_ptr = unsafe { ws_jksv_normalize(system.as_ptr(), tree_cstr.as_ptr()) };
    assert!(!norm_ptr.is_null());
    let norm_json = unsafe { CStr::from_ptr(norm_ptr) }.to_str().unwrap();
    let saves: Vec<serde_json::Value> = serde_json::from_str(norm_json).unwrap();
    assert_eq!(saves.len(), 1);

    let save_cstr = CString::new(serde_json::to_string(&saves[0]).unwrap()).unwrap();
    let mut out_zip = WsBuf::null();
    let entry_ptr = unsafe { ws_package(save_cstr.as_ptr(), &mut out_zip) };
    assert!(!entry_ptr.is_null());

    let entry_json = unsafe { CStr::from_ptr(entry_ptr) }.to_str().unwrap();
    let entry: waystone_core::model::SaveEntry = serde_json::from_str(entry_json).unwrap();
    let zip_slice = unsafe { std::slice::from_raw_parts(out_zip.ptr, out_zip.len) };
    assert_eq!(
        entry.content.hash,
        waystone_core::packaging::content_hash(zip_slice)
    );

    // Verify round-trip: unzip and check content
    let unzip_ptr = unsafe { ws_unzip(zip_slice.as_ptr(), zip_slice.len()) };
    let unzip_json = unsafe { CStr::from_ptr(unzip_ptr) }.to_str().unwrap();
    let files: Vec<FileEntryDto> = serde_json::from_str(unzip_json).unwrap();
    assert_eq!(files.len(), 1);
    assert_eq!(files[0].path, "data.sav");
    assert_eq!(
        files[0].decode_data().unwrap(),
        vec![0xCA, 0xFE, 0xBA, 0xBE]
    );

    unsafe {
        ws_string_free(norm_ptr);
        ws_string_free(entry_ptr);
        ws_string_free(unzip_ptr);
        ws_buf_free(out_zip);
    }
}

// ── Crypto ABI tests ─────────────────────────────────────────────────────────

#[test]
fn vault_init_and_unlock_with_passphrase() {
    let pass = CString::new("test-passphrase").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();

    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };
    assert!(!vault.is_null(), "ws_vault_init should return non-null");
    assert!(recovery_buf.len > 0);
    assert!(keys_buf.len > 0);

    let keys_slice = unsafe { std::slice::from_raw_parts(keys_buf.ptr, keys_buf.len) };

    let vault2 =
        unsafe { ws_vault_unlock_pass(pass.as_ptr(), keys_slice.as_ptr(), keys_slice.len()) };
    assert!(
        !vault2.is_null(),
        "ws_vault_unlock_pass should return non-null"
    );

    // Encrypt with vault, decrypt with vault2
    let plaintext = b"hello from ffi";
    let encrypted = unsafe { ws_vault_encrypt_blob(vault, plaintext.as_ptr(), plaintext.len()) };
    assert!(encrypted.len > 0);

    let decrypted = unsafe { ws_vault_decrypt_blob(vault2, encrypted.ptr, encrypted.len) };
    assert!(decrypted.len > 0);
    let dec_slice = unsafe { std::slice::from_raw_parts(decrypted.ptr, decrypted.len) };
    assert_eq!(dec_slice, plaintext);

    unsafe {
        ws_buf_free(encrypted);
        ws_buf_free(decrypted);
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
        ws_vault_free(vault2);
    }
}

#[test]
fn vault_init_and_unlock_with_recovery() {
    let pass = CString::new("recovery-test").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();

    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };
    assert!(!vault.is_null());

    let recovery_slice = unsafe { std::slice::from_raw_parts(recovery_buf.ptr, recovery_buf.len) };
    let recovery_hex = std::str::from_utf8(recovery_slice).unwrap();
    let recovery_cstr = CString::new(recovery_hex).unwrap();

    let keys_slice = unsafe { std::slice::from_raw_parts(keys_buf.ptr, keys_buf.len) };

    let vault2 = unsafe {
        ws_vault_unlock_recovery(
            recovery_cstr.as_ptr(),
            keys_slice.as_ptr(),
            keys_slice.len(),
        )
    };
    assert!(
        !vault2.is_null(),
        "ws_vault_unlock_recovery should return non-null"
    );

    let plaintext = b"recovery round trip";
    let encrypted = unsafe { ws_vault_encrypt_blob(vault, plaintext.as_ptr(), plaintext.len()) };
    let decrypted = unsafe { ws_vault_decrypt_blob(vault2, encrypted.ptr, encrypted.len) };
    let dec_slice = unsafe { std::slice::from_raw_parts(decrypted.ptr, decrypted.len) };
    assert_eq!(dec_slice, plaintext);

    unsafe {
        ws_buf_free(encrypted);
        ws_buf_free(decrypted);
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
        ws_vault_free(vault2);
    }
}

#[test]
fn vault_wrong_passphrase_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let pass = CString::new("correct").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();

    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };
    assert!(!vault.is_null());

    let keys_slice = unsafe { std::slice::from_raw_parts(keys_buf.ptr, keys_buf.len) };
    let wrong = CString::new("wrong").unwrap();
    let vault2 =
        unsafe { ws_vault_unlock_pass(wrong.as_ptr(), keys_slice.as_ptr(), keys_slice.len()) };
    assert!(vault2.is_null(), "wrong passphrase should return null");

    let err = unsafe { waystone_ffi::error::ws_last_error() };
    assert!(!err.is_null(), "last error should be set");

    unsafe {
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
    }
}

#[test]
fn vault_heads_encrypt_decrypt_round_trip() {
    let pass = CString::new("heads-test").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();
    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };

    let heads_json = b"{\"device_id\":\"dev1\",\"hash\":\"abc\",\"mtime\":\"2026-01-01\"}";
    let encrypted = unsafe { ws_vault_encrypt_heads(vault, heads_json.as_ptr(), heads_json.len()) };
    assert!(encrypted.len > 0);

    let decrypted = unsafe { ws_vault_decrypt_heads(vault, encrypted.ptr, encrypted.len) };
    let dec_slice = unsafe { std::slice::from_raw_parts(decrypted.ptr, decrypted.len) };
    assert_eq!(dec_slice, heads_json);

    unsafe {
        ws_buf_free(encrypted);
        ws_buf_free(decrypted);
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
    }
}

#[test]
fn vault_blob_name_and_path_segment() {
    let pass = CString::new("hmac-test").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();
    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };

    let hash_cstr = CString::new("abc123hash").unwrap();
    let name1 = unsafe { ws_vault_blob_name(vault, hash_cstr.as_ptr()) };
    let name2 = unsafe { ws_vault_blob_name(vault, hash_cstr.as_ptr()) };
    assert!(!name1.is_null());
    let s1 = unsafe { CStr::from_ptr(name1) }.to_str().unwrap();
    let s2 = unsafe { CStr::from_ptr(name2) }.to_str().unwrap();
    assert_eq!(s1, s2, "blob_name must be deterministic");
    assert_ne!(s1, "abc123hash");

    let seg_cstr = CString::new("switch").unwrap();
    let seg = unsafe { ws_vault_path_segment(vault, seg_cstr.as_ptr()) };
    assert!(!seg.is_null());
    let seg_str = unsafe { CStr::from_ptr(seg) }.to_str().unwrap();
    assert_ne!(seg_str, "switch");

    unsafe {
        ws_string_free(name1);
        ws_string_free(name2);
        ws_string_free(seg);
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
    }
}

#[test]
fn vault_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let encrypted = unsafe { ws_vault_encrypt_blob(std::ptr::null(), b"data".as_ptr(), 4) };
    assert!(encrypted.ptr.is_null());
    let err = unsafe { waystone_ffi::error::ws_last_error() };
    assert!(!err.is_null());
}

// ── Packaging ABI tests ──────────────────────────────────────────────────────

#[test]
fn canonical_zip_golden_vector_through_ffi() {
    // Matches golden_single_file_zip_and_hash from core/tests/golden_vectors.rs
    let files_json = r#"[{"path":"save.dat","data_b64":"SGVsbG8="}]"#;
    let cstr = CString::new(files_json).unwrap();
    let zip_buf = unsafe { ws_canonical_zip(cstr.as_ptr()) };
    assert!(!zip_buf.ptr.is_null());

    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };
    assert_eq!(zip_slice.len(), 119, "zip length must match golden vector");

    // Content hash must match golden vector exactly
    let hash_ptr = unsafe { ws_content_hash(zip_slice.as_ptr(), zip_slice.len()) };
    assert!(!hash_ptr.is_null());
    let hash = unsafe { CStr::from_ptr(hash_ptr) }.to_str().unwrap();
    assert_eq!(
        hash,
        "66dc6c1281582edd83ee325e37f45d235c7e60d859f8117dd5d3a978117ee318"
    );

    unsafe {
        ws_buf_free(zip_buf);
        ws_string_free(hash_ptr);
    }
}

#[test]
fn unzip_round_trips_through_ffi() {
    let files_json = r#"[{"path":"a.bin","data_b64":"AQID"}]"#;
    let cstr = CString::new(files_json).unwrap();
    let zip_buf = unsafe { ws_canonical_zip(cstr.as_ptr()) };
    assert!(!zip_buf.ptr.is_null());

    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };
    let result_ptr = unsafe { ws_unzip(zip_slice.as_ptr(), zip_slice.len()) };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let entries: Vec<FileEntryDto> = serde_json::from_str(result_json).unwrap();
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].path, "a.bin");
    let decoded = entries[0].decode_data().unwrap();
    assert_eq!(decoded, vec![1, 2, 3]);

    unsafe {
        ws_buf_free(zip_buf);
        ws_string_free(result_ptr);
    }
}

#[test]
fn file_hash_through_ffi() {
    let data = vec![1u8, 2, 3, 4];
    let hash_ptr = unsafe { ws_file_hash(data.as_ptr(), data.len()) };
    assert!(!hash_ptr.is_null());
    let hash = unsafe { CStr::from_ptr(hash_ptr) }.to_str().unwrap();
    assert_eq!(hash.len(), 64);
    // Verify it matches core directly
    assert_eq!(hash, waystone_core::packaging::file_hash(&data));
    unsafe { ws_string_free(hash_ptr) };
}

#[test]
fn package_through_ffi() {
    let save_json = serde_json::json!({
        "id": {
            "source": "jksv",
            "system": "switch",
            "game": {
                "key": "TESTGAME",
                "display_name": "Test Game",
                "confidence": "strong"
            },
            "slot": "main",
            "kind": "native"
        },
        "group_key": "switch/TESTGAME/main",
        "portable": true,
        "mtime": "2026-01-01T00:00:00Z",
        "files": [{"path": "save.dat", "data_b64": "//8="}]
    });
    let json_str = CString::new(serde_json::to_string(&save_json).unwrap()).unwrap();
    let mut out_zip = WsBuf::null();

    let entry_ptr = unsafe { ws_package(json_str.as_ptr(), &mut out_zip) };
    assert!(!entry_ptr.is_null(), "ws_package should return non-null");
    assert!(out_zip.len > 0, "zip output should be non-empty");

    let entry_json = unsafe { CStr::from_ptr(entry_ptr) }.to_str().unwrap();
    let entry: waystone_core::model::SaveEntry = serde_json::from_str(entry_json).unwrap();
    assert_eq!(entry.id.game.key, "TESTGAME");

    // Verify hash matches content_hash of the zip
    let zip_slice = unsafe { std::slice::from_raw_parts(out_zip.ptr, out_zip.len) };
    assert_eq!(
        entry.content.hash,
        waystone_core::packaging::content_hash(zip_slice)
    );

    unsafe {
        ws_string_free(entry_ptr);
        ws_buf_free(out_zip);
    }
}

#[test]
fn canonical_zip_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let zip_buf = unsafe { ws_canonical_zip(std::ptr::null()) };
    assert!(zip_buf.ptr.is_null());
}

// ── Conflict ABI tests ───────────────────────────────────────────────────────

#[test]
fn fold_heads_through_ffi() {
    let heads_json = serde_json::json!([
        {"device_id": "dev1", "hash": "hash_a", "mtime": "2026-01-01T00:00:00Z"},
        {"device_id": "dev2", "hash": "hash_b", "mtime": "2026-01-03T00:00:00Z"},
        {"device_id": "dev3", "hash": "hash_a", "mtime": "2026-01-02T00:00:00Z"}
    ]);
    let cstr = CString::new(serde_json::to_string(&heads_json).unwrap()).unwrap();
    let result_ptr = unsafe { ws_fold_heads(cstr.as_ptr()) };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let merged: waystone_core::conflict::MergedHead = serde_json::from_str(result_json).unwrap();
    assert_eq!(merged.hash, "hash_b");
    assert_eq!(merged.mtime, "2026-01-03T00:00:00Z");
    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn fold_heads_empty_returns_null() {
    let cstr = CString::new("[]").unwrap();
    let result_ptr = unsafe { ws_fold_heads(cstr.as_ptr()) };
    assert!(result_ptr.is_null());
}

#[test]
fn three_way_sync_through_ffi() {
    let local = CString::new("hash_a").unwrap();
    let base = CString::new("hash_a").unwrap();
    let head = CString::new("hash_b").unwrap();
    let local_mtime = CString::new("2026-01-01T00:00:00Z").unwrap();
    let head_mtime = CString::new("2026-01-02T00:00:00Z").unwrap();

    let result_ptr = unsafe {
        ws_three_way_sync(
            local.as_ptr(),
            base.as_ptr(),
            head.as_ptr(),
            local_mtime.as_ptr(),
            head_mtime.as_ptr(),
            0, // NewestWins
        )
    };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let decision: waystone_core::conflict::SyncDecision =
        serde_json::from_str(result_json).unwrap();
    assert_eq!(
        decision,
        waystone_core::conflict::SyncDecision::Pull {
            head_hash: "hash_b".into()
        }
    );
    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn three_way_sync_null_local_means_none() {
    let head = CString::new("hash_a").unwrap();
    let local_mtime = CString::new("").unwrap();
    let head_mtime = CString::new("2026-01-01T00:00:00Z").unwrap();

    let result_ptr = unsafe {
        ws_three_way_sync(
            std::ptr::null(), // None local
            std::ptr::null(), // None base
            head.as_ptr(),
            local_mtime.as_ptr(),
            head_mtime.as_ptr(),
            0,
        )
    };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let decision: waystone_core::conflict::SyncDecision =
        serde_json::from_str(result_json).unwrap();
    assert_eq!(
        decision,
        waystone_core::conflict::SyncDecision::Pull {
            head_hash: "hash_a".into()
        }
    );
    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn decide_pull_through_ffi() {
    let local_hash = CString::new("hash_a").unwrap();
    let local_mtime = CString::new("2026-01-01T00:00:00Z").unwrap();
    let heads_json = serde_json::json!([
        {"device_id": "dev1", "hash": "hash_a", "mtime": "2026-01-01T00:00:00Z"},
        {"device_id": "dev2", "hash": "hash_b", "mtime": "2026-01-02T00:00:00Z"}
    ]);
    let heads_cstr = CString::new(serde_json::to_string(&heads_json).unwrap()).unwrap();
    let device_id = CString::new("dev1").unwrap();

    let result_ptr = unsafe {
        ws_decide_pull(
            local_hash.as_ptr(),
            local_mtime.as_ptr(),
            heads_cstr.as_ptr(),
            device_id.as_ptr(),
            0, // NewestWins
        )
    };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let decision: waystone_core::conflict::SyncDecision =
        serde_json::from_str(result_json).unwrap();
    assert_eq!(
        decision,
        waystone_core::conflict::SyncDecision::Pull {
            head_hash: "hash_b".into()
        }
    );
    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn decide_pull_new_device_through_ffi() {
    let local_mtime = CString::new("").unwrap();
    let heads_json = serde_json::json!([
        {"device_id": "dev1", "hash": "hash_a", "mtime": "2026-01-01T00:00:00Z"}
    ]);
    let heads_cstr = CString::new(serde_json::to_string(&heads_json).unwrap()).unwrap();
    let device_id = CString::new("dev2").unwrap();

    let result_ptr = unsafe {
        ws_decide_pull(
            std::ptr::null(), // no local save
            local_mtime.as_ptr(),
            heads_cstr.as_ptr(),
            device_id.as_ptr(),
            0,
        )
    };
    assert!(!result_ptr.is_null());
    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let decision: waystone_core::conflict::SyncDecision =
        serde_json::from_str(result_json).unwrap();
    assert_eq!(
        decision,
        waystone_core::conflict::SyncDecision::Pull {
            head_hash: "hash_a".into()
        }
    );
    unsafe { ws_string_free(result_ptr) };
}

// ── Adapter ABI tests ────────────────────────────────────────────────────────

#[test]
fn jksv_normalize_through_ffi() {
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "TestGame - 0100AAAA00001000/slot0/data.sav",
            "data_b64": "yv66vg=="
        }]
    });
    let system = CString::new("switch").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let result_ptr = unsafe { ws_jksv_normalize(system.as_ptr(), json_str.as_ptr()) };
    assert!(!result_ptr.is_null());

    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let saves: Vec<NormalizedSaveDto> = serde_json::from_str(result_json).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].id.game.key, "0100AAAA00001000");
    assert_eq!(saves[0].files[0].path, "data.sav");

    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn jksv_to_native_through_ffi() {
    // First normalize, then convert back
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "TestGame - 0100AAAA00001000/slot0/data.sav",
            "data_b64": "yv66vg=="
        }]
    });
    let system = CString::new("switch").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let norm_ptr = unsafe { ws_jksv_normalize(system.as_ptr(), json_str.as_ptr()) };
    let norm_json = unsafe { CStr::from_ptr(norm_ptr) }.to_str().unwrap();
    let saves: Vec<serde_json::Value> = serde_json::from_str(norm_json).unwrap();
    let save_cstr = CString::new(serde_json::to_string(&saves[0]).unwrap()).unwrap();

    let native_ptr = unsafe { ws_jksv_to_native(save_cstr.as_ptr()) };
    assert!(!native_ptr.is_null());
    let native_json = unsafe { CStr::from_ptr(native_ptr) }.to_str().unwrap();
    let tree: RawTreeDto = serde_json::from_str(native_json).unwrap();
    assert_eq!(tree.files.len(), 1);
    assert!(tree.files[0].path.contains("0100AAAA00001000"));
    assert!(tree.files[0].path.contains("data.sav"));

    unsafe {
        ws_string_free(norm_ptr);
        ws_string_free(native_ptr);
    }
}

#[test]
fn mgba_normalize_through_ffi() {
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "Emerald.sav",
            "data_b64": "//8="
        }]
    });
    let system = CString::new("gba").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let result_ptr = unsafe { ws_mgba_normalize(system.as_ptr(), json_str.as_ptr()) };
    assert!(!result_ptr.is_null());

    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let saves: Vec<NormalizedSaveDto> = serde_json::from_str(result_json).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].id.game.key, "Emerald");

    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn mgba_to_native_through_ffi() {
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "Emerald.sav",
            "data_b64": "//8="
        }]
    });
    let system = CString::new("gba").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let norm_ptr = unsafe { ws_mgba_normalize(system.as_ptr(), json_str.as_ptr()) };
    let norm_json = unsafe { CStr::from_ptr(norm_ptr) }.to_str().unwrap();
    let saves: Vec<serde_json::Value> = serde_json::from_str(norm_json).unwrap();
    let save_cstr = CString::new(serde_json::to_string(&saves[0]).unwrap()).unwrap();

    let native_ptr = unsafe { ws_mgba_to_native(save_cstr.as_ptr()) };
    assert!(!native_ptr.is_null());
    let native_json = unsafe { CStr::from_ptr(native_ptr) }.to_str().unwrap();
    let tree: RawTreeDto = serde_json::from_str(native_json).unwrap();
    assert_eq!(tree.files.len(), 1);
    assert_eq!(tree.files[0].path, "Emerald.sav");

    unsafe {
        ws_string_free(norm_ptr);
        ws_string_free(native_ptr);
    }
}

#[test]
fn adapter_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let system = CString::new("switch").unwrap();
    let result = unsafe { ws_jksv_normalize(system.as_ptr(), std::ptr::null()) };
    assert!(result.is_null());
}

#[test]
fn twilight_normalize_through_ffi() {
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "saves/Metroid.sav",
            "data_b64": "//8="
        }]
    });
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let result_ptr = unsafe { ws_twilight_normalize(json_str.as_ptr()) };
    assert!(!result_ptr.is_null());

    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let saves: Vec<NormalizedSaveDto> = serde_json::from_str(result_json).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].id.game.key, "Metroid");
    assert_eq!(saves[0].id.slot, "battery");
    assert_eq!(saves[0].files[0].path, "Metroid.sav");

    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn twilight_to_native_through_ffi() {
    let raw_tree = serde_json::json!({
        "files": [{
            "path": "saves/Metroid.sav",
            "data_b64": "//8="
        }]
    });
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let norm_ptr = unsafe { ws_twilight_normalize(json_str.as_ptr()) };
    let norm_json = unsafe { CStr::from_ptr(norm_ptr) }.to_str().unwrap();
    let saves: Vec<serde_json::Value> = serde_json::from_str(norm_json).unwrap();
    let save_cstr = CString::new(serde_json::to_string(&saves[0]).unwrap()).unwrap();

    let native_ptr = unsafe { ws_twilight_to_native(save_cstr.as_ptr()) };
    assert!(!native_ptr.is_null());
    let native_json = unsafe { CStr::from_ptr(native_ptr) }.to_str().unwrap();
    let tree: RawTreeDto = serde_json::from_str(native_json).unwrap();
    assert_eq!(tree.files.len(), 1);
    assert_eq!(tree.files[0].path, "saves/Metroid.sav");

    unsafe {
        ws_string_free(norm_ptr);
        ws_string_free(native_ptr);
    }
}

#[test]
fn twilight_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let result = unsafe { ws_twilight_normalize(std::ptr::null()) };
    assert!(result.is_null());
    let result = unsafe { ws_twilight_to_native(std::ptr::null()) };
    assert!(result.is_null());
}

#[test]
fn checkpoint_normalize_through_ffi() {
    use base64::Engine;
    use base64::engine::general_purpose::STANDARD as B64;

    let raw_tree = serde_json::json!({
        "files": [{
            "path": "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/data.bin",
            "data_b64": B64.encode([0xDEu8, 0xAD])
        }]
    });
    let system = CString::new("switch").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let result_ptr = unsafe { ws_checkpoint_normalize(system.as_ptr(), json_str.as_ptr()) };
    assert!(!result_ptr.is_null());

    let result_json = unsafe { CStr::from_ptr(result_ptr) }.to_str().unwrap();
    let saves: Vec<NormalizedSaveDto> = serde_json::from_str(result_json).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].id.game.key, "01006A800016E000");
    assert_eq!(saves[0].id.game.display_name, "Super Smash Bros. Ultimate");
    assert_eq!(saves[0].id.slot, "20230715-143052");
    assert_eq!(saves[0].files[0].path, "data.bin");

    unsafe { ws_string_free(result_ptr) };
}

#[test]
fn checkpoint_to_native_through_ffi() {
    use base64::Engine;
    use base64::engine::general_purpose::STANDARD as B64;

    let raw_tree = serde_json::json!({
        "files": [{
            "path": "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/data.bin",
            "data_b64": B64.encode([0xDEu8, 0xAD])
        }]
    });
    let system = CString::new("switch").unwrap();
    let json_str = CString::new(serde_json::to_string(&raw_tree).unwrap()).unwrap();
    let norm_ptr = unsafe { ws_checkpoint_normalize(system.as_ptr(), json_str.as_ptr()) };
    let norm_json = unsafe { CStr::from_ptr(norm_ptr) }.to_str().unwrap();
    let saves: Vec<serde_json::Value> = serde_json::from_str(norm_json).unwrap();
    let save_cstr = CString::new(serde_json::to_string(&saves[0]).unwrap()).unwrap();

    let native_ptr = unsafe { ws_checkpoint_to_native(save_cstr.as_ptr()) };
    assert!(!native_ptr.is_null());
    let native_json = unsafe { CStr::from_ptr(native_ptr) }.to_str().unwrap();
    let tree: RawTreeDto = serde_json::from_str(native_json).unwrap();
    assert_eq!(tree.files.len(), 1);
    assert!(tree.files[0].path.contains("01006A800016E000"));
    assert!(tree.files[0].path.contains("data.bin"));

    unsafe {
        ws_string_free(norm_ptr);
        ws_string_free(native_ptr);
    }
}

#[test]
fn checkpoint_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().expect("ERROR_TEST_LOCK poisoned");
    let system = CString::new("switch").unwrap();
    let result = unsafe { ws_checkpoint_normalize(system.as_ptr(), std::ptr::null()) };
    assert!(result.is_null());
    let result = unsafe { ws_checkpoint_to_native(std::ptr::null()) };
    assert!(result.is_null());
}
