use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use waystone_mobile::decision::{ConflictPolicy, PushOutcome, SyncDecision};
use waystone_mobile::error::WaystoneError;
use waystone_mobile::exports::{jksv_normalize, pull_one, push_one};
use waystone_mobile::types::{Confidence, FileEntry, NormalizedSave, RawFileEntry, RawTree};
use waystone_mobile::vault::Vault;
use waystone_mobile::webdav::WebDav;

struct MockWebDav {
    store: Mutex<HashMap<String, Vec<u8>>>,
}

impl MockWebDav {
    fn new() -> Self {
        Self {
            store: Mutex::new(HashMap::new()),
        }
    }
}

impl WebDav for MockWebDav {
    fn get(&self, path: String) -> Result<Option<Vec<u8>>, WaystoneError> {
        Ok(self.store.lock().unwrap().get(&path).cloned())
    }

    fn put(&self, path: String, body: Vec<u8>) -> Result<(), WaystoneError> {
        self.store.lock().unwrap().insert(path, body);
        Ok(())
    }

    fn exists(&self, path: String) -> Result<bool, WaystoneError> {
        Ok(self.store.lock().unwrap().contains_key(&path))
    }

    fn propfind(&self, path: String) -> Result<Vec<String>, WaystoneError> {
        let store = self.store.lock().unwrap();
        let prefix = if path.ends_with('/') {
            path.clone()
        } else {
            format!("{}/", path)
        };
        let mut hrefs: Vec<String> = store
            .keys()
            .filter(|k| k.starts_with(&prefix) && k[prefix.len()..].find('/').is_none())
            .cloned()
            .collect();
        hrefs.sort();
        Ok(hrefs)
    }

    fn mkdir_p(&self, _path: String) -> Result<(), WaystoneError> {
        Ok(())
    }
}

fn make_mobile_save(content: &[u8], mtime: &str) -> NormalizedSave {
    NormalizedSave {
        source: "jksv".into(),
        system: "switch".into(),
        game_key: "TEST_GAME".into(),
        display_name: "Test Game".into(),
        title_id: Some("TEST_GAME".into()),
        serial: None,
        rom_crc: None,
        confidence: Confidence::Strong,
        slot: "main".into(),
        kind: "native".into(),
        group_key: "switch/TEST_GAME/main".into(),
        portable: true,
        mtime: mtime.into(),
        files: vec![FileEntry {
            path: "save.dat".into(),
            content: content.to_vec(),
        }],
    }
}

#[test]
fn vault_init_and_unlock_round_trip() {
    let init_result = Vault::init("test-passphrase".into()).unwrap();
    let vault1 = init_result.vault;
    let keys_json = init_result.keys_json;
    let recovery_hex = init_result.recovery_hex;

    let vault2 =
        Vault::unlock_with_passphrase("test-passphrase".into(), keys_json.clone()).unwrap();

    let plaintext = b"hello vault".to_vec();
    let encrypted = vault1.encrypt_blob(plaintext.clone()).unwrap();
    let decrypted = vault2.decrypt_blob(encrypted).unwrap();
    assert_eq!(decrypted, plaintext);

    let vault3 = Vault::unlock_with_recovery(recovery_hex, keys_json).unwrap();
    let encrypted2 = vault3.encrypt_blob(plaintext.clone()).unwrap();
    let decrypted2 = vault1.decrypt_blob(encrypted2).unwrap();
    assert_eq!(decrypted2, plaintext);
}

#[test]
fn vault_blob_name_deterministic() {
    let init = Vault::init("det-test".into()).unwrap();
    let name1 = init.vault.blob_name("hash123".into());
    let name2 = init.vault.blob_name("hash123".into());
    assert_eq!(name1, name2);
    assert!(!name1.is_empty());
}

#[test]
fn mobile_push_and_pull_round_trip() {
    let init = Vault::init("mobile-test".into()).unwrap();
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());

    let save = make_mobile_save(b"mobile-content", "2026-01-01T00:00:00Z");
    let outcome = push_one(
        init.vault.clone(),
        save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();
    assert!(matches!(outcome, PushOutcome::Pushed));

    let save_v2 = make_mobile_save(b"updated-content", "2026-01-02T00:00:00Z");
    push_one(init.vault.clone(), save_v2, "dev1".into(), Arc::clone(&dav)).unwrap();

    let pull_result = pull_one(
        init.vault.clone(),
        save.clone(),
        "dev2".into(),
        Arc::clone(&dav),
        ConflictPolicy::NewestWins,
    )
    .unwrap();

    match &pull_result.decision {
        SyncDecision::Pull { .. } | SyncDecision::ConflictResolved { .. } => {}
        other => panic!("expected Pull or ConflictResolved, got {:?}", other),
    }
    let files = pull_result.files.expect("should have pulled files");
    let save_file = files
        .iter()
        .find(|f| f.path == "save.dat")
        .expect("save.dat");
    assert_eq!(save_file.content, b"updated-content");
}

#[test]
fn jksv_normalize_produces_saves() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "TestGame - 0100AAAA00001000/slot0/data.sav".into(),
            content: vec![0xCA, 0xFE],
        }],
    };
    let saves = jksv_normalize(raw);
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].system, "switch");
    assert_eq!(saves[0].slot, "slot0");
    assert!(!saves[0].files.is_empty());
}

#[test]
fn push_blob_existed_on_second_identical_push() {
    let init = Vault::init("dedup-test".into()).unwrap();
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let save = make_mobile_save(b"same-content", "2026-01-01T00:00:00Z");

    let first = push_one(
        init.vault.clone(),
        save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();
    assert!(matches!(first, PushOutcome::Pushed));

    let second = push_one(
        init.vault.clone(),
        save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();
    assert!(matches!(second, PushOutcome::BlobExisted));
}

#[test]
fn pull_in_sync_when_already_up_to_date() {
    let init = Vault::init("insync-test".into()).unwrap();
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let save = make_mobile_save(b"same-content", "2026-01-01T00:00:00Z");

    push_one(
        init.vault.clone(),
        save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();

    let result = pull_one(
        init.vault.clone(),
        save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
        ConflictPolicy::NewestWins,
    )
    .unwrap();
    assert!(matches!(result.decision, SyncDecision::InSync));
    assert!(result.files.is_none());
}

// Fix 1: parse_system must error on unknown system strings.
#[test]
fn parse_system_errors_on_unknown_system() {
    let init = Vault::init("err-test".into()).unwrap();
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let mut save = make_mobile_save(b"content", "2026-01-01T00:00:00Z");
    save.system = "megadrive".into();
    let result = push_one(init.vault, save, "dev1".into(), dav);
    assert!(matches!(result, Err(WaystoneError::InvalidSystem { .. })));
}

// Fix 2: pull_one must accept an explicit ConflictPolicy parameter.
// Push a newer version with dev1, then pull with dev2 holding an older version.
#[test]
fn pull_one_accepts_conflict_policy_newest_wins() {
    let init = Vault::init("policy-test".into()).unwrap();
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());

    let old_save = make_mobile_save(b"old-content", "2026-01-01T00:00:00Z");
    push_one(
        init.vault.clone(),
        old_save.clone(),
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();

    let new_save = make_mobile_save(b"new-content", "2026-01-02T00:00:00Z");
    push_one(
        init.vault.clone(),
        new_save,
        "dev1".into(),
        Arc::clone(&dav),
    )
    .unwrap();

    let result = pull_one(
        init.vault,
        old_save,
        "dev2".into(),
        Arc::clone(&dav),
        ConflictPolicy::NewestWins,
    )
    .unwrap();
    assert!(
        matches!(
            result.decision,
            SyncDecision::Pull { .. } | SyncDecision::ConflictResolved { .. }
        ),
        "expected Pull or ConflictResolved, got {:?}",
        result.decision
    );
}

// Fix 3: jksv_normalize populates confidence from the adapter output (not hardcoded).
// JKSV sets Strong when the directory name is a pure 16-char hex title ID.
#[test]
fn jksv_normalize_populates_confidence_from_adapter() {
    // Pure 16-char hex dir → parse_title_dir extracts title_id → Strong confidence.
    let raw_strong = RawTree {
        files: vec![RawFileEntry {
            path: "0100AAAA00001000/slot0/data.sav".into(),
            content: vec![0xCA, 0xFE],
        }],
    };
    let saves = jksv_normalize(raw_strong);
    assert_eq!(saves.len(), 1);
    assert!(
        matches!(saves[0].confidence, Confidence::Strong),
        "expected Strong for pure-hex dir, got {:?}",
        saves[0].confidence
    );
    assert!(saves[0].serial.is_none());
    assert!(saves[0].rom_crc.is_none());

    // Display-name dir → no title_id → Weak confidence.
    let raw_weak = RawTree {
        files: vec![RawFileEntry {
            path: "TestGame/slot0/data.sav".into(),
            content: vec![0xBE, 0xEF],
        }],
    };
    let saves_weak = jksv_normalize(raw_weak);
    assert_eq!(saves_weak.len(), 1);
    assert!(
        matches!(saves_weak[0].confidence, Confidence::Weak),
        "expected Weak for name-only dir, got {:?}",
        saves_weak[0].confidence
    );
}
