use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::adapters::Adapter;
use waystone_core::crypto::Vault;
use waystone_core::model::*;
use waystone_core::packaging::{canonical_zip, content_hash, package, unzip};

/// Vector 1: single file, known content -> exact zip bytes -> exact hash
#[test]
fn golden_single_file_zip_and_hash() {
    let files = vec![("save.dat".to_string(), vec![0x48u8, 0x65, 0x6C, 0x6C, 0x6F])];
    let zip_bytes = canonical_zip(&files);
    let hash = content_hash(&zip_bytes);

    // local(30) + name(8) + content(5) + central(46) + name(8) + eocd(22) = 119
    assert_eq!(zip_bytes.len(), 119, "zip length must be deterministic");

    let zip_bytes_2 = canonical_zip(&files);
    assert_eq!(zip_bytes, zip_bytes_2);

    assert_eq!(hash, "66dc6c1281582edd83ee325e37f45d235c7e60d859f8117dd5d3a978117ee318");
}

/// Vector 2: multi-file zip, order independence
#[test]
fn golden_multi_file_zip_order_independent() {
    let files_a = vec![
        ("b.bin".to_string(), vec![2u8]),
        ("a.bin".to_string(), vec![1u8]),
    ];
    let files_b = vec![
        ("a.bin".to_string(), vec![1u8]),
        ("b.bin".to_string(), vec![2u8]),
    ];
    let zip_a = canonical_zip(&files_a);
    let zip_b = canonical_zip(&files_b);
    assert_eq!(zip_a, zip_b, "entry order must not affect output");
    assert_eq!(content_hash(&zip_a), content_hash(&zip_b));
}

/// Vector 3: full pipeline (adapter -> package -> encrypt -> decrypt -> unpackage)
#[test]
fn golden_full_pipeline_round_trip() {
    let raw = RawTree {
        files: vec![RawFile {
            path: "TestGame - 0100AAAA00001000/slot0/data.sav".into(),
            content: vec![0xCA, 0xFE, 0xBA, 0xBE],
        }],
    };

    let adapter = JksvAdapter::new(SystemId::Switch);
    let normalized = adapter.normalize(&raw);
    assert_eq!(normalized.len(), 1);

    let (entry, zip_bytes) = package(&normalized[0]);
    assert!(!zip_bytes.is_empty());
    assert_eq!(entry.content.hash, content_hash(&zip_bytes));

    let (vault, _recovery) = Vault::init("golden-test-passphrase").unwrap();
    let encrypted = vault.encrypt_blob(&zip_bytes).unwrap();
    assert_ne!(encrypted, zip_bytes);

    let decrypted = vault.decrypt_blob(&encrypted).unwrap();
    assert_eq!(decrypted, zip_bytes);

    let restored_files = unzip(&decrypted).unwrap();
    assert_eq!(restored_files.len(), 1);
    assert_eq!(restored_files[0].0, "data.sav");
    assert_eq!(restored_files[0].1, vec![0xCA, 0xFE, 0xBA, 0xBE]);
}

/// Vector 4: HMAC blob names are deterministic with same vault
#[test]
fn golden_hmac_determinism() {
    let (vault, _recovery) = Vault::init("determinism-test").unwrap();
    let keys_json = vault.keys_json().unwrap();

    let vault2 = Vault::unlock_with_passphrase("determinism-test", &keys_json).unwrap();

    let name1 = vault.blob_name("abc123");
    let name2 = vault2.blob_name("abc123");
    assert_eq!(name1, name2);

    let seg1 = vault.path_segment("switch");
    let seg2 = vault2.path_segment("switch");
    assert_eq!(seg1, seg2);
}

/// Vector 5: mgba adapter round-trip
#[test]
fn golden_mgba_round_trip() {
    let raw = RawTree {
        files: vec![RawFile {
            path: "Emerald.sav".into(),
            content: vec![0xFF; 64],
        }],
    };

    let adapter = MgbaAdapter::new(SystemId::Gba);
    let normalized = adapter.normalize(&raw);
    assert_eq!(normalized.len(), 1);
    assert_eq!(normalized[0].id.game.key, "Emerald");

    let (entry, zip_bytes) = package(&normalized[0]);
    let hash = content_hash(&zip_bytes);
    assert_eq!(entry.content.hash, hash);

    let (entry2, zip_bytes2) = package(&normalized[0]);
    assert_eq!(zip_bytes, zip_bytes2);
    assert_eq!(entry.content.hash, entry2.content.hash);
}
