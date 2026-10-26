use std::collections::HashMap;
use std::sync::{Arc, Mutex, PoisonError};
use waystone_mobile::decision::ConflictPolicy;
use waystone_mobile::engine::{
    PlanAction, RomRef, SaveIo, SyncPhase, SyncProgressSink, SyncSummary, engine_execute,
    engine_forced_push, engine_plan, engine_restore_hash, remote_only_saves, sync_summary_headline,
};
use waystone_mobile::error::WaystoneError;
use waystone_mobile::exports::{
    local_hash, nds_banner_icon_rgba, nds_banner_len, nds_icon_side, push_one, read_remote_heads,
    retroarch_boxart_path, rom_system_badge, twilight_boxart_paths, version_newer,
};
use waystone_mobile::types::{Confidence, FileEntry, NormalizedSave};
use waystone_mobile::vault::vault_init;
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
        Ok(self
            .store
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .get(&path)
            .cloned())
    }

    fn put(&self, path: String, body: Vec<u8>) -> Result<(), WaystoneError> {
        self.store
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .insert(path, body);
        Ok(())
    }

    fn exists(&self, path: String) -> Result<bool, WaystoneError> {
        Ok(self
            .store
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .contains_key(&path))
    }

    fn propfind(&self, path: String) -> Result<Vec<String>, WaystoneError> {
        let store = self.store.lock().unwrap_or_else(PoisonError::into_inner);
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

#[derive(Default)]
struct RecIo {
    restored: Mutex<Vec<u32>>,
}
impl RecIo {
    fn count(&self) -> usize {
        self.restored
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .len()
    }
}
impl SaveIo for RecIo {
    fn restore(
        &self,
        index: u32,
        _save: NormalizedSave,
        _files: Vec<FileEntry>,
    ) -> Result<bool, WaystoneError> {
        self.restored
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(index);
        Ok(true)
    }
}

#[derive(Default)]
struct RecSink {
    phases: Mutex<Vec<SyncPhase>>,
}
impl RecSink {
    fn phases(&self) -> Vec<SyncPhase> {
        self.phases
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .clone()
    }
}
impl SyncProgressSink for RecSink {
    fn started(&self, _total: u32) -> Result<(), WaystoneError> {
        Ok(())
    }
    fn item(
        &self,
        _index: u32,
        _total: u32,
        _group_key: String,
        _display_name: String,
        phase: SyncPhase,
    ) -> Result<(), WaystoneError> {
        self.phases
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(phase);
        Ok(())
    }
    fn is_cancelled(&self) -> Result<bool, WaystoneError> {
        Ok(false)
    }
}

fn ui_gone() -> WaystoneError {
    WaystoneError::WebDav {
        msg: "ui gone".into(),
    }
}

struct FailingSink;
impl SyncProgressSink for FailingSink {
    fn started(&self, _total: u32) -> Result<(), WaystoneError> {
        Err(ui_gone())
    }
    fn item(
        &self,
        _index: u32,
        _total: u32,
        _group_key: String,
        _display_name: String,
        _phase: SyncPhase,
    ) -> Result<(), WaystoneError> {
        Err(ui_gone())
    }
    fn is_cancelled(&self) -> Result<bool, WaystoneError> {
        Err(ui_gone())
    }
}

#[test]
fn engine_execute_survives_a_failing_progress_sink() {
    let v = vault_init("e".into()).unwrap().vault;
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let save = make_mobile_save(b"data", "2026-01-01T00:00:00Z");
    let plan = engine_plan(
        v.clone(),
        vec![save],
        "dev1".into(),
        ConflictPolicy::NewestWins,
        dav.clone(),
    )
    .unwrap();
    let summary = engine_execute(
        v,
        plan,
        "dev1".into(),
        dav,
        Arc::new(RecIo::default()),
        Arc::new(FailingSink),
    )
    .unwrap();
    assert_eq!(summary.pushed, 1);
    assert!(!summary.cancelled);
}

#[test]
fn engine_plan_execute_pull_writes_own_head() {
    let v = vault_init("e".into()).unwrap().vault;
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let old = make_mobile_save(b"old", "2026-01-01T00:00:00Z");
    push_one(v.clone(), old.clone(), "dev1".into(), dav.clone()).unwrap();
    let mut newer = old.clone();
    newer.files = vec![FileEntry {
        path: "save.dat".into(),
        content: b"new".to_vec(),
    }];
    newer.mtime = "2026-02-01T00:00:00Z".into();
    push_one(v.clone(), newer, "dev2".into(), dav.clone()).unwrap();

    let plan = engine_plan(
        v.clone(),
        vec![old.clone()],
        "dev1".into(),
        ConflictPolicy::Prompt,
        dav.clone(),
    )
    .unwrap();
    assert_eq!(plan[0].action, PlanAction::Pull);
    let io = Arc::new(RecIo::default());
    let sink = Arc::new(RecSink::default());
    let s = engine_execute(
        v.clone(),
        plan,
        "dev1".into(),
        dav.clone(),
        io.clone(),
        sink.clone(),
    )
    .unwrap();
    assert_eq!(s.pulled, 1);
    assert_eq!(io.count(), 1);
    let heads = read_remote_heads(v.clone(), old.clone(), dav.clone()).unwrap();
    assert!(
        heads
            .iter()
            .any(|h| h.device_id == "dev1" && h.mtime == "2026-02-01T00:00:00Z")
    );
    assert_eq!(
        sink.phases(),
        vec![SyncPhase::Pulling, SyncPhase::Restoring]
    );
}

#[test]
fn engine_restore_hash_writes_files_without_touching_own_head() {
    let v = vault_init("r".into()).unwrap().vault;
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let save = make_mobile_save(b"history", "2026-01-01T00:00:00Z");
    push_one(v.clone(), save.clone(), "dev2".into(), dav.clone()).unwrap();
    let hash = local_hash(save.clone()).unwrap();
    let io = Arc::new(RecIo::default());
    assert!(engine_restore_hash(v.clone(), save.clone(), hash, dav.clone(), io.clone()).unwrap());
    assert_eq!(io.count(), 1);
    let heads = read_remote_heads(v.clone(), save.clone(), dav.clone()).unwrap();
    assert!(heads.iter().all(|h| h.device_id != "dev1"));
    let missing = engine_restore_hash(v, save, "0000".into(), dav, io);
    assert!(matches!(missing, Err(WaystoneError::BlobNotFound { .. })));
}

#[test]
fn forced_push_and_headline() {
    let save = make_mobile_save(b"x", "2026-01-01T00:00:00Z");
    assert!(engine_forced_push(save.clone()).unwrap().is_some());
    let mut ro = save;
    ro.files.clear();
    assert!(engine_forced_push(ro).unwrap().is_none());
    let s = SyncSummary {
        pushed: 1,
        pulled: 0,
        in_sync: 0,
        conflicts: 0,
        errors: 0,
        error_messages: vec![],
        cancelled: false,
    };
    assert_eq!(sync_summary_headline(s), "Done \u{2014} 1 uploaded");
}

#[test]
fn remote_only_rejects_unknown_system() {
    let v = vault_init("e".into()).unwrap().vault;
    let dav: Arc<dyn WebDav> = Arc::new(MockWebDav::new());
    let roms = vec![RomRef {
        system: "bogus".into(),
        rom_id: "X".into(),
        display_name: "X".into(),
    }];
    assert!(remote_only_saves(v, roms, vec![], dav).is_err());
}

#[test]
fn art_and_version_exports() {
    assert!(version_newer("1.10.0".into(), "1.9.0".into()));
    assert_eq!(rom_system_badge("nds".into()), "DS");
    assert_eq!(twilight_boxart_paths("a.nds".into()).len(), 2);
    assert!(
        retroarch_boxart_path("switch".into(), "x".into())
            .unwrap()
            .is_none()
    );
    assert!(retroarch_boxart_path("bogus".into(), "x".into()).is_err());
    assert!(nds_banner_icon_rgba(vec![0; 0x100]).is_none());
    let side = nds_icon_side() as usize;
    let icon = nds_banner_icon_rgba(vec![0; nds_banner_len() as usize]).unwrap();
    assert_eq!(icon.len(), side * side * 4);
}
