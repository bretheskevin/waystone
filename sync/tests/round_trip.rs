use std::collections::HashMap;
use std::sync::Mutex;
use waystone_core::conflict::SyncDecision;
use waystone_core::crypto::Vault;
use waystone_core::model::*;
use waystone_sync::{PushOutcome, Result, WebDav};

struct InMemoryDav {
    store: Mutex<HashMap<String, Vec<u8>>>,
}

impl InMemoryDav {
    fn new() -> Self {
        Self {
            store: Mutex::new(HashMap::new()),
        }
    }

    fn keys(&self) -> Vec<String> {
        self.store.lock().unwrap().keys().cloned().collect()
    }
}

impl WebDav for InMemoryDav {
    fn get(&self, path: &str) -> Result<Option<Vec<u8>>> {
        Ok(self.store.lock().unwrap().get(path).cloned())
    }

    fn put(&self, path: &str, body: Vec<u8>) -> Result<()> {
        self.store.lock().unwrap().insert(path.to_string(), body);
        Ok(())
    }

    fn exists(&self, path: &str) -> Result<bool> {
        Ok(self.store.lock().unwrap().contains_key(path))
    }

    fn propfind(&self, path: &str) -> Result<Vec<String>> {
        let store = self.store.lock().unwrap();
        let prefix = if path.ends_with('/') {
            path.to_string()
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

    fn mkdir_p(&self, _path: &str) -> Result<()> {
        Ok(())
    }
}

fn make_test_save(content: &[u8], mtime: &str) -> NormalizedSave {
    NormalizedSave {
        id: SaveId {
            source: "jksv".into(),
            system: SystemId::Switch,
            game: GameRef {
                key: "TEST_GAME".into(),
                display_name: "Test Game".into(),
                confidence: Confidence::Strong,
                title_id: Some("TEST_GAME".into()),
                serial: None,
                rom_crc: None,
            },
            slot: "main".into(),
            kind: SaveKind::Native,
        },
        group_key: "switch/TEST_GAME/main".into(),
        portable: true,
        mtime: mtime.into(),
        files: vec![("save.dat".into(), content.to_vec())],
    }
}

#[test]
fn push_one_creates_blob_head_and_history() {
    let (vault, _) = Vault::init("test-pass").unwrap();
    let dav = InMemoryDav::new();
    let save = make_test_save(b"hello-world", "2026-01-01T00:00:00Z");

    let outcome = waystone_sync::push_one(&vault, &save, "dev1", &dav).unwrap();
    assert!(matches!(outcome, PushOutcome::Pushed));

    let keys = dav.keys();
    assert!(
        keys.iter().any(|k| k.contains("/blobs/")),
        "must create a blob: {keys:?}"
    );
    assert!(
        keys.iter().any(|k| k.contains("/heads/dev1.json")),
        "must create a head: {keys:?}"
    );
    assert!(
        keys.iter().any(|k| k.contains("/history/")),
        "must create a history entry: {keys:?}"
    );
}

#[test]
fn read_remote_heads_after_push() {
    let (vault, _) = Vault::init("test-pass").unwrap();
    let dav = InMemoryDav::new();
    let save = make_test_save(b"content-a", "2026-01-01T00:00:00Z");

    waystone_sync::push_one(&vault, &save, "dev1", &dav).unwrap();

    let heads = waystone_sync::read_remote_heads(&vault, &save, &dav).unwrap();
    assert_eq!(heads.len(), 1);
    assert_eq!(heads[0].device_id, "dev1");
}

#[test]
fn pull_one_returns_files_when_remote_is_newer() {
    let (vault, _) = Vault::init("test-pass").unwrap();
    let dav = InMemoryDav::new();

    let save_v1 = make_test_save(b"old-content", "2026-01-01T00:00:00Z");
    waystone_sync::push_one(&vault, &save_v1, "dev1", &dav).unwrap();
    waystone_sync::push_one(&vault, &save_v1, "dev2", &dav).unwrap();

    let save_v2 = make_test_save(b"new-content", "2026-01-02T00:00:00Z");
    waystone_sync::push_one(&vault, &save_v2, "dev1", &dav).unwrap();

    let outcome = waystone_sync::pull_one(
        &vault,
        &save_v1,
        "dev2",
        waystone_core::conflict::ConflictPolicy::NewestWins,
        &dav,
    )
    .unwrap();

    assert!(
        matches!(outcome.decision, SyncDecision::Pull { .. }),
        "expected Pull, got {:?}",
        outcome.decision
    );
    let files = outcome.files.expect("pull should return files");
    let save_file = files
        .iter()
        .find(|(p, _)| p == "save.dat")
        .expect("save.dat");
    assert_eq!(save_file.1, b"new-content");
}

#[test]
fn two_device_push_pull_round_trip() {
    let (vault1, _) = Vault::init("round-trip-pass").unwrap();
    let keys_json = vault1.keys_json().unwrap();
    let vault2 = Vault::unlock_with_passphrase("round-trip-pass", &keys_json).unwrap();
    let dav = InMemoryDav::new();

    let save_old = make_test_save(b"shared-initial-save", "2026-01-01T00:00:00Z");

    waystone_sync::push_one(&vault1, &save_old, "device-1", &dav).unwrap();
    waystone_sync::push_one(&vault2, &save_old, "device-2", &dav).unwrap();

    let save_new = make_test_save(b"device1-progress-content", "2026-01-02T00:00:00Z");
    waystone_sync::push_one(&vault1, &save_new, "device-1", &dav).unwrap();

    let outcome = waystone_sync::pull_one(
        &vault2,
        &save_old,
        "device-2",
        waystone_core::conflict::ConflictPolicy::NewestWins,
        &dav,
    )
    .unwrap();

    let hash = match &outcome.decision {
        SyncDecision::Pull { head_hash } => head_hash.clone(),
        SyncDecision::ConflictResolved {
            winner: waystone_core::conflict::ConflictWinner::Remote,
            ..
        } => {
            let heads = waystone_sync::read_remote_heads(&vault2, &save_old, &dav).unwrap();
            waystone_core::conflict::fold_heads(&heads).unwrap().hash
        }
        other => panic!("expected Pull or ConflictResolved(Remote), got {:?}", other),
    };
    assert!(!hash.is_empty());

    let files = outcome.files.expect("should have pulled files");
    let save_file = files
        .iter()
        .find(|(p, _)| p == "save.dat")
        .expect("save.dat");
    assert_eq!(
        save_file.1, b"device1-progress-content",
        "device-2 should have received device-1's updated save"
    );
}

#[test]
fn conflict_needs_input_when_both_devices_diverged() {
    let (vault, _) = Vault::init("conflict-pass").unwrap();
    let dav = InMemoryDav::new();

    let base_save = make_test_save(b"base-content", "2026-01-01T00:00:00Z");
    waystone_sync::push_one(&vault, &base_save, "dev1", &dav).unwrap();
    waystone_sync::push_one(&vault, &base_save, "dev2", &dav).unwrap();

    let dev1_save = make_test_save(b"dev1-changed", "2026-01-02T00:00:00Z");
    waystone_sync::push_one(&vault, &dev1_save, "dev1", &dav).unwrap();

    let dev2_save = make_test_save(b"dev2-changed", "2026-01-03T00:00:00Z");

    let outcome = waystone_sync::pull_one(
        &vault,
        &dev2_save,
        "dev2",
        waystone_core::conflict::ConflictPolicy::Prompt,
        &dav,
    )
    .unwrap();

    assert!(
        matches!(outcome.decision, SyncDecision::ConflictNeedsInput { .. }),
        "expected ConflictNeedsInput, got {:?}",
        outcome.decision
    );
    assert!(outcome.files.is_none(), "no files when conflict unresolved");
}
