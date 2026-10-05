use std::ffi::{CStr, CString};
use std::sync::Mutex;

use waystone_ffi::adapter_abi::*;
use waystone_ffi::buffer::{WsBuf, ws_buf_free, ws_string_free};
use waystone_ffi::conflict_abi::*;
use waystone_ffi::crypto_abi::*;
use waystone_ffi::error::{catch_and_set_error, set_last_error, ws_last_error};
use waystone_ffi::packaging_abi::*;
use waystone_ffi::wire::{SaveList, decode_file_tree, decode_save_list, encode_file_tree};

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
    let tree = encode_file_tree(&[("save.dat".to_string(), b"Hello".to_vec())]);
    let zip_buf = unsafe { ws_canonical_zip(tree.as_ptr(), tree.len()) };
    assert!(!zip_buf.ptr.is_null());
    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };

    let core_files = vec![("save.dat".to_string(), vec![0x48u8, 0x65, 0x6C, 0x6C, 0x6F])];
    let core_zip = waystone_core::packaging::canonical_zip(&core_files);
    assert_eq!(
        zip_slice,
        core_zip.as_slice(),
        "FFI zip must be byte-identical to core zip"
    );

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
    let tree_a = encode_file_tree(&[
        ("b.bin".to_string(), vec![2u8]),
        ("a.bin".to_string(), vec![1u8]),
    ]);
    let tree_b = encode_file_tree(&[
        ("a.bin".to_string(), vec![1u8]),
        ("b.bin".to_string(), vec![2u8]),
    ]);
    let zip_a = unsafe { ws_canonical_zip(tree_a.as_ptr(), tree_a.len()) };
    let zip_b = unsafe { ws_canonical_zip(tree_b.as_ptr(), tree_b.len()) };
    let slice_a = unsafe { std::slice::from_raw_parts(zip_a.ptr, zip_a.len) };
    let slice_b = unsafe { std::slice::from_raw_parts(zip_b.ptr, zip_b.len) };
    assert_eq!(
        slice_a, slice_b,
        "entry order must not affect FFI zip output"
    );

    let core_zip = waystone_core::packaging::canonical_zip(&[
        ("b.bin".to_string(), vec![2u8]),
        ("a.bin".to_string(), vec![1u8]),
    ]);
    assert_eq!(slice_a, core_zip.as_slice());
    unsafe {
        ws_buf_free(zip_a);
        ws_buf_free(zip_b);
    }
}

/// Golden Vector 3 parity: full pipeline (adapter -> package) through FFI
#[test]
fn golden_ffi_full_pipeline_jksv_package() {
    let raw = encode_file_tree(&[(
        "TestGame - 0100AAAA00001000/slot0/data.sav".to_string(),
        vec![0xCAu8, 0xFE, 0xBA, 0xBE],
    )]);
    let system = CString::new("switch").unwrap();
    let list_buf = unsafe { ws_jksv_normalize(system.as_ptr(), raw.as_ptr(), raw.len()) };
    assert!(!list_buf.ptr.is_null());
    let list_slice = unsafe { std::slice::from_raw_parts(list_buf.ptr, list_buf.len) };
    let saves = decode_save_list(list_slice).unwrap();
    assert_eq!(saves.len(), 1);

    let (meta_bytes, files) = &saves[0];
    let meta_cstr = CString::new(meta_bytes.clone()).unwrap();
    let files_buf = encode_file_tree(files);
    let mut out_zip = WsBuf::null();
    let entry_ptr = unsafe {
        ws_package(
            meta_cstr.as_ptr(),
            files_buf.as_ptr(),
            files_buf.len(),
            &mut out_zip,
        )
    };
    assert!(!entry_ptr.is_null());

    let entry_json = unsafe { CStr::from_ptr(entry_ptr) }.to_str().unwrap();
    let entry: waystone_core::model::SaveEntry = serde_json::from_str(entry_json).unwrap();
    let zip_slice = unsafe { std::slice::from_raw_parts(out_zip.ptr, out_zip.len) };
    assert_eq!(
        entry.content.hash,
        waystone_core::packaging::content_hash(zip_slice)
    );

    let ft_buf = unsafe { ws_unzip(zip_slice.as_ptr(), zip_slice.len()) };
    let ft_slice = unsafe { std::slice::from_raw_parts(ft_buf.ptr, ft_buf.len) };
    let out_files = decode_file_tree(ft_slice).unwrap();
    assert_eq!(out_files.len(), 1);
    assert_eq!(out_files[0].0, "data.sav");
    assert_eq!(out_files[0].1, vec![0xCA, 0xFE, 0xBA, 0xBE]);

    unsafe {
        ws_buf_free(list_buf);
        ws_string_free(entry_ptr);
        ws_buf_free(out_zip);
        ws_buf_free(ft_buf);
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

#[test]
fn vault_export_mdk_round_trip_via_from_mdk() {
    let pass = CString::new("mdk-round-trip").unwrap();
    let mut recovery_buf = WsBuf::null();
    let mut keys_buf = WsBuf::null();

    let vault = unsafe { ws_vault_init(pass.as_ptr(), &mut recovery_buf, &mut keys_buf) };
    assert!(!vault.is_null());

    // Export MDK
    let mdk_buf = unsafe { ws_vault_export_mdk(vault) };
    assert!(!mdk_buf.ptr.is_null());
    assert_eq!(mdk_buf.len, 32);

    // Reconstruct from MDK
    let vault2 = unsafe { ws_vault_from_mdk(mdk_buf.ptr, mdk_buf.len) };
    assert!(!vault2.is_null());

    // Verify encrypt/decrypt parity
    let plaintext = b"mdk ffi round trip";
    let encrypted = unsafe { ws_vault_encrypt_blob(vault, plaintext.as_ptr(), plaintext.len()) };
    assert!(encrypted.len > 0);
    let decrypted = unsafe { ws_vault_decrypt_blob(vault2, encrypted.ptr, encrypted.len) };
    let dec_slice = unsafe { std::slice::from_raw_parts(decrypted.ptr, decrypted.len) };
    assert_eq!(dec_slice, plaintext);

    unsafe {
        ws_buf_free(mdk_buf);
        ws_buf_free(encrypted);
        ws_buf_free(decrypted);
        ws_buf_free(recovery_buf);
        ws_buf_free(keys_buf);
        ws_vault_free(vault);
        ws_vault_free(vault2);
    }
}

#[test]
fn vault_from_mdk_null_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let vault = unsafe { ws_vault_from_mdk(std::ptr::null(), 32) };
    assert!(vault.is_null());
    let err = unsafe { ws_last_error() };
    assert!(!err.is_null());
}

#[test]
fn vault_from_mdk_wrong_length_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let bad_mdk = [0u8; 16];
    let vault = unsafe { ws_vault_from_mdk(bad_mdk.as_ptr(), bad_mdk.len()) };
    assert!(vault.is_null());
    let err = unsafe { ws_last_error() };
    assert!(!err.is_null());
}

#[test]
fn vault_export_mdk_null_vault_returns_null() {
    let buf = unsafe { ws_vault_export_mdk(std::ptr::null()) };
    assert!(buf.ptr.is_null());
    assert_eq!(buf.len, 0);
}

// ── Packaging ABI tests ──────────────────────────────────────────────────────

#[test]
fn canonical_zip_golden_vector_through_ffi() {
    let tree = encode_file_tree(&[("save.dat".to_string(), b"Hello".to_vec())]);
    let zip_buf = unsafe { ws_canonical_zip(tree.as_ptr(), tree.len()) };
    assert!(!zip_buf.ptr.is_null());
    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };
    assert_eq!(zip_slice.len(), 119, "zip length must match golden vector");

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

#[test]
fn unzip_round_trips_through_ffi() {
    let tree = encode_file_tree(&[("a.bin".to_string(), vec![1u8, 2, 3])]);
    let zip_buf = unsafe { ws_canonical_zip(tree.as_ptr(), tree.len()) };
    assert!(!zip_buf.ptr.is_null());
    let zip_slice = unsafe { std::slice::from_raw_parts(zip_buf.ptr, zip_buf.len) };

    let ft_buf = unsafe { ws_unzip(zip_slice.as_ptr(), zip_slice.len()) };
    assert!(!ft_buf.ptr.is_null());
    let ft_slice = unsafe { std::slice::from_raw_parts(ft_buf.ptr, ft_buf.len) };
    let entries = decode_file_tree(ft_slice).unwrap();
    assert_eq!(entries.len(), 1);
    assert_eq!(entries[0].0, "a.bin");
    assert_eq!(entries[0].1, vec![1, 2, 3]);

    unsafe {
        ws_buf_free(zip_buf);
        ws_buf_free(ft_buf);
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
    let meta = serde_json::json!({
        "id": {"source":"jksv","system":"switch",
               "game":{"key":"TESTGAME","display_name":"Test Game","confidence":"strong"},
               "slot":"main","kind":"native"},
        "group_key":"switch/TESTGAME/main",
        "portable":true,
        "mtime":"2026-01-01T00:00:00Z"
    });
    let meta_cstr = CString::new(serde_json::to_string(&meta).unwrap()).unwrap();
    let tree = encode_file_tree(&[("save.dat".to_string(), vec![0xFFu8, 0xFF])]);
    let mut out_zip = WsBuf::null();

    let entry_ptr =
        unsafe { ws_package(meta_cstr.as_ptr(), tree.as_ptr(), tree.len(), &mut out_zip) };
    assert!(!entry_ptr.is_null(), "ws_package should return non-null");
    assert!(out_zip.len > 0, "zip output should be non-empty");

    let entry_json = unsafe { CStr::from_ptr(entry_ptr) }.to_str().unwrap();
    let entry: waystone_core::model::SaveEntry = serde_json::from_str(entry_json).unwrap();
    assert_eq!(entry.id.game.key, "TESTGAME");
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
    let zip_buf = unsafe { ws_canonical_zip(std::ptr::null(), 0) };
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

fn normalize_one(
    f: unsafe extern "C" fn(*const std::ffi::c_char, *const u8, usize) -> WsBuf,
    system: &str,
    path: &str,
    data: &[u8],
) -> (WsBuf, SaveList) {
    let raw = encode_file_tree(&[(path.to_string(), data.to_vec())]);
    let system = CString::new(system).unwrap();
    let buf = unsafe { f(system.as_ptr(), raw.as_ptr(), raw.len()) };
    assert!(!buf.ptr.is_null());
    let slice = unsafe { std::slice::from_raw_parts(buf.ptr, buf.len) };
    let saves = decode_save_list(slice).unwrap();
    (buf, saves)
}

type ToNativeFn =
    unsafe extern "C" fn(*const std::ffi::c_char, *const u8, usize, *mut WsBuf) -> i32;

fn to_native_files(
    f: ToNativeFn,
    save: &(Vec<u8>, Vec<(String, Vec<u8>)>),
) -> Vec<(String, Vec<u8>)> {
    let meta = CString::new(save.0.clone()).unwrap();
    let files = encode_file_tree(&save.1);
    let mut out = WsBuf::null();
    let rc = unsafe { f(meta.as_ptr(), files.as_ptr(), files.len(), &mut out) };
    assert_eq!(rc, 0);
    let slice = unsafe { std::slice::from_raw_parts(out.ptr, out.len) };
    let tree = decode_file_tree(slice).unwrap();
    unsafe { ws_buf_free(out) };
    tree
}

fn assert_to_native_null_rejected(f: ToNativeFn) {
    let mut out = WsBuf::null();
    let rc = unsafe { f(std::ptr::null(), std::ptr::null(), 0, &mut out) };
    assert_eq!(rc, -1);
}

const CHECKPOINT_PATH: &str =
    "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/data.bin";

#[test]
fn jksv_normalize_through_ffi() {
    let raw = encode_file_tree(&[(
        "TestGame - 0100AAAA00001000/slot0/data.sav".to_string(),
        vec![0xCAu8, 0xFE, 0xBA, 0xBE],
    )]);
    let system = CString::new("switch").unwrap();
    let list_buf = unsafe { ws_jksv_normalize(system.as_ptr(), raw.as_ptr(), raw.len()) };
    assert!(!list_buf.ptr.is_null());
    let list_slice = unsafe { std::slice::from_raw_parts(list_buf.ptr, list_buf.len) };
    let saves = decode_save_list(list_slice).unwrap();
    assert_eq!(saves.len(), 1);
    let meta: serde_json::Value = serde_json::from_slice(&saves[0].0).unwrap();
    assert_eq!(meta["id"]["game"]["key"], "testgame0100aaaa00001000");
    assert_eq!(saves[0].1[0].0, "data.sav");
    unsafe { ws_buf_free(list_buf) };
}

#[test]
fn jksv_to_native_through_ffi() {
    let raw = encode_file_tree(&[(
        "TestGame - 0100AAAA00001000/slot0/data.sav".to_string(),
        vec![0xCAu8, 0xFE, 0xBA, 0xBE],
    )]);
    let system = CString::new("switch").unwrap();
    let list_buf = unsafe { ws_jksv_normalize(system.as_ptr(), raw.as_ptr(), raw.len()) };
    let list_slice = unsafe { std::slice::from_raw_parts(list_buf.ptr, list_buf.len) };
    let saves = decode_save_list(list_slice).unwrap();
    let meta_cstr = CString::new(saves[0].0.clone()).unwrap();
    let files_buf = encode_file_tree(&saves[0].1);

    let mut out_tree = WsBuf::null();
    let rc = unsafe {
        ws_jksv_to_native(
            meta_cstr.as_ptr(),
            files_buf.as_ptr(),
            files_buf.len(),
            &mut out_tree,
        )
    };
    assert_eq!(rc, 0);
    let out_slice = unsafe { std::slice::from_raw_parts(out_tree.ptr, out_tree.len) };
    let tree = decode_file_tree(out_slice).unwrap();
    assert_eq!(tree.len(), 1);
    assert!(tree[0].0.contains("0100AAAA00001000"));
    assert!(tree[0].0.contains("data.sav"));

    unsafe {
        ws_buf_free(list_buf);
        ws_buf_free(out_tree);
    }
}

#[test]
fn mgba_normalize_through_ffi() {
    let (buf, saves) = normalize_one(ws_mgba_normalize, "gba", "Emerald.sav", &[0xFF, 0xFF]);
    assert_eq!(saves.len(), 1);
    let meta: serde_json::Value = serde_json::from_slice(&saves[0].0).unwrap();
    assert_eq!(meta["id"]["game"]["key"], "Emerald");
    unsafe { ws_buf_free(buf) };
}

#[test]
fn mgba_to_native_through_ffi() {
    let (buf, saves) = normalize_one(ws_mgba_normalize, "gba", "Emerald.sav", &[0xFF, 0xFF]);
    let tree = to_native_files(ws_mgba_to_native, &saves[0]);
    assert_eq!(tree.len(), 1);
    assert_eq!(tree[0].0, "Emerald.sav");
    unsafe { ws_buf_free(buf) };
}

#[test]
fn adapter_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let system = CString::new("switch").unwrap();
    let result = unsafe { ws_jksv_normalize(system.as_ptr(), std::ptr::null(), 0) };
    assert!(result.ptr.is_null());
    assert_to_native_null_rejected(ws_jksv_to_native);
}

#[test]
fn twilight_normalize_through_ffi() {
    let raw = encode_file_tree(&[("saves/Metroid.sav".to_string(), vec![0xFFu8, 0xFF])]);
    let buf = unsafe { ws_twilight_normalize(raw.as_ptr(), raw.len()) };
    assert!(!buf.ptr.is_null());
    let slice = unsafe { std::slice::from_raw_parts(buf.ptr, buf.len) };
    let saves = decode_save_list(slice).unwrap();
    assert_eq!(saves.len(), 1);
    let meta: serde_json::Value = serde_json::from_slice(&saves[0].0).unwrap();
    assert_eq!(meta["id"]["game"]["key"], "Metroid");
    assert_eq!(meta["id"]["slot"], "battery");
    assert_eq!(saves[0].1[0].0, "Metroid.sav");
    unsafe { ws_buf_free(buf) };
}

#[test]
fn twilight_to_native_through_ffi() {
    let raw = encode_file_tree(&[("saves/Metroid.sav".to_string(), vec![0xFFu8, 0xFF])]);
    let buf = unsafe { ws_twilight_normalize(raw.as_ptr(), raw.len()) };
    let slice = unsafe { std::slice::from_raw_parts(buf.ptr, buf.len) };
    let saves = decode_save_list(slice).unwrap();
    let tree = to_native_files(ws_twilight_to_native, &saves[0]);
    assert_eq!(tree.len(), 1);
    assert_eq!(tree[0].0, "saves/Metroid.sav");
    unsafe { ws_buf_free(buf) };
}

#[test]
fn twilight_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().unwrap();
    let result = unsafe { ws_twilight_normalize(std::ptr::null(), 0) };
    assert!(result.ptr.is_null());
    assert_to_native_null_rejected(ws_twilight_to_native);
}

#[test]
fn checkpoint_normalize_through_ffi() {
    let (buf, saves) = normalize_one(
        ws_checkpoint_normalize,
        "switch",
        CHECKPOINT_PATH,
        &[0xDE, 0xAD],
    );
    assert_eq!(saves.len(), 1);
    let meta: serde_json::Value = serde_json::from_slice(&saves[0].0).unwrap();
    assert_eq!(meta["id"]["game"]["key"], "supersmashbrosultimate");
    assert_eq!(
        meta["id"]["game"]["display_name"],
        "Super Smash Bros. Ultimate"
    );
    assert_eq!(meta["id"]["slot"], "20230715-143052");
    assert_eq!(saves[0].1[0].0, "data.bin");
    unsafe { ws_buf_free(buf) };
}

#[test]
fn checkpoint_to_native_through_ffi() {
    let (buf, saves) = normalize_one(
        ws_checkpoint_normalize,
        "switch",
        CHECKPOINT_PATH,
        &[0xDE, 0xAD],
    );
    let tree = to_native_files(ws_checkpoint_to_native, &saves[0]);
    assert_eq!(tree.len(), 1);
    assert!(tree[0].0.contains("01006A800016E000"));
    assert!(tree[0].0.contains("data.bin"));
    unsafe { ws_buf_free(buf) };
}

#[test]
fn checkpoint_null_input_returns_null() {
    let _guard = ERROR_TEST_LOCK.lock().expect("ERROR_TEST_LOCK poisoned");
    let system = CString::new("switch").unwrap();
    let result = unsafe { ws_checkpoint_normalize(system.as_ptr(), std::ptr::null(), 0) };
    assert!(result.ptr.is_null());
    assert_to_native_null_rejected(ws_checkpoint_to_native);
}
