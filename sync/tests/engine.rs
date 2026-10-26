use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Mutex, PoisonError};
use waystone_core::conflict::{ConflictPolicy, DeviceHead, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::*;
use waystone_sync::engine::{
    self, PlanAction, RestoreError, SaveIo, SyncPhase, SyncProgressSink, SyncSummary,
};
use waystone_sync::remote_only::{RomRef, remote_only_saves};
use waystone_sync::testing::MemoryDav;
use waystone_sync::{
    Result as SyncResult, SyncError, WebDav, orchestration::remote_path, push_one,
};

fn vault() -> Vault {
    Vault::init("engine-tests").unwrap().0
}

fn save(key: &str, content: &str, mtime: &str) -> NormalizedSave {
    NormalizedSave {
        id: SaveId {
            source: "jksv".into(),
            system: SystemId::Switch,
            game: GameRef {
                key: key.into(),
                display_name: key.into(),
                confidence: Confidence::Strong,
                title_id: None,
                serial: None,
                rom_crc: None,
            },
            slot: "main".into(),
            kind: SaveKind::Native,
        },
        group_key: format!("switch/{key}/main"),
        portable: true,
        mtime: mtime.into(),
        files: vec![("save.dat".into(), content.as_bytes().to_vec())],
    }
}

fn with_content(s: &NormalizedSave, content: &str, mtime: &str) -> NormalizedSave {
    let mut c = s.clone();
    c.files = vec![("save.dat".into(), content.as_bytes().to_vec())];
    c.mtime = mtime.into();
    c
}

const T1: &str = "2026-01-01T00:00:00Z";
const T2: &str = "2026-02-01T00:00:00Z";

/// Fails every request whose path contains `needle`.
struct FailOn<'a> {
    inner: &'a MemoryDav,
    needle: String,
}
impl WebDav for FailOn<'_> {
    fn get(&self, p: &str) -> SyncResult<Option<Vec<u8>>> {
        self.check(p)?;
        self.inner.get(p)
    }
    fn put(&self, p: &str, b: Vec<u8>) -> SyncResult<()> {
        self.check(p)?;
        self.inner.put(p, b)
    }
    fn exists(&self, p: &str) -> SyncResult<bool> {
        self.check(p)?;
        self.inner.exists(p)
    }
    fn propfind(&self, p: &str) -> SyncResult<Vec<String>> {
        self.check(p)?;
        self.inner.propfind(p)
    }
    fn mkdir_p(&self, p: &str) -> SyncResult<()> {
        self.check(p)?;
        self.inner.mkdir_p(p)
    }
}
impl FailOn<'_> {
    fn check(&self, p: &str) -> SyncResult<()> {
        if p.contains(&self.needle) {
            Err(SyncError::WebDav(format!("boom {p}")))
        } else {
            Ok(())
        }
    }
}

type Restored = (usize, String, Vec<(String, Vec<u8>)>);

#[derive(Default)]
struct RecIo {
    restored: Mutex<Vec<Restored>>,
    fail_first: AtomicBool,
}
impl SaveIo for RecIo {
    fn restore(
        &self,
        index: usize,
        save: &NormalizedSave,
        files: Vec<(String, Vec<u8>)>,
    ) -> Result<bool, String> {
        if self.fail_first.swap(false, Ordering::SeqCst) {
            return Ok(false);
        }
        self.restored
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push((index, save.group_key.clone(), files));
        Ok(true)
    }
}

#[derive(Default)]
struct RecSink {
    events: Mutex<Vec<String>>,
    cancel_after: Option<usize>,
    items: AtomicUsize,
}
impl SyncProgressSink for RecSink {
    fn started(&self, total: usize) {
        self.events
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(format!("started {total}"));
    }
    fn item(&self, index: usize, total: usize, save: &NormalizedSave, phase: SyncPhase) {
        self.items.fetch_add(1, Ordering::SeqCst);
        self.events
            .lock()
            .unwrap_or_else(PoisonError::into_inner)
            .push(format!("{index}/{total} {} {phase:?}", save.id.game.key));
    }
    fn is_cancelled(&self) -> bool {
        self.cancel_after
            .is_some_and(|n| self.items.load(Ordering::SeqCst) >= n)
    }
}

fn decrypt_head(v: &Vault, dav: &MemoryDav, s: &NormalizedSave, device: &str) -> DeviceHead {
    let path = format!("{}/heads/{device}.json", remote_path(v, s));
    serde_json::from_slice(&v.decrypt_heads(&dav.read(&path).unwrap()).unwrap()).unwrap()
}

#[test]
fn plan_covers_push_insync_pull_conflict_and_error() {
    let v = vault();
    let dav = MemoryDav::new();
    let synced = save("SYNCED", "same", T1);
    push_one(&v, &synced, "dev1", &dav).unwrap();
    let behind = save("BEHIND", "old", T1);
    push_one(&v, &behind, "dev1", &dav).unwrap();
    push_one(&v, &with_content(&behind, "new", T2), "dev2", &dav).unwrap();
    let conflict = save("CONFLICT", "mine", T1);
    push_one(&v, &with_content(&conflict, "theirs", T1), "dev2", &dav).unwrap();
    let fresh = save("FRESH", "local only", T1);
    let broken = save("BROKEN", "x", T1);
    let failing = FailOn {
        inner: &dav,
        needle: v.path_segment("BROKEN"),
    };

    let plan = engine::plan(
        &v,
        &[fresh, synced, behind, conflict, broken],
        "dev1",
        ConflictPolicy::Prompt,
        &failing,
    );

    let actions: Vec<_> = plan.iter().map(|e| e.action).collect();
    assert_eq!(
        actions,
        vec![
            PlanAction::Push,
            PlanAction::InSync,
            PlanAction::Pull,
            PlanAction::Conflict,
            PlanAction::Error
        ]
    );
    assert!(!plan[2].pull_hash.as_deref().unwrap().is_empty());
    assert_eq!(plan[2].head_mtime, T2);
    assert!(matches!(
        plan[3].decision,
        Some(SyncDecision::ConflictNeedsInput { .. })
    ));
    assert!(plan[4].error.as_deref().unwrap().contains("boom"));
}

#[test]
fn newest_wins_conflict_with_newer_remote_pulls_merged_head() {
    let v = vault();
    let dav = MemoryDav::new();
    let mine = save("G", "mine", T1);
    push_one(&v, &with_content(&mine, "theirs", T2), "dev2", &dav).unwrap();
    let e = engine::plan_one(&v, &mine, "dev1", ConflictPolicy::NewestWins, &dav);
    assert_eq!(e.action, PlanAction::Pull);
    assert_eq!(
        e.pull_hash,
        e.heads
            .iter()
            .find(|h| h.device_id == "dev2")
            .map(|h| h.hash.clone())
    );
}

#[test]
fn newest_wins_with_unknown_local_mtime_is_treated_as_prompt() {
    let v = vault();
    let dav = MemoryDav::new();
    let mine = save("G", "mine", "");
    push_one(&v, &with_content(&mine, "theirs", T2), "dev2", &dav).unwrap();
    let e = engine::plan_one(&v, &mine, "dev1", ConflictPolicy::NewestWins, &dav);
    assert_eq!(e.action, PlanAction::Conflict);
}

#[test]
fn remote_only_save_plans_a_pull_and_is_never_force_pushed() {
    let v = vault();
    let dav = MemoryDav::new();
    let s = save("ROM", "remote", T1);
    push_one(&v, &s, "dev2", &dav).unwrap();
    let mut remote_only = s.clone();
    remote_only.files.clear();
    remote_only.mtime.clear();
    assert!(engine::is_remote_only(&remote_only));
    let e = engine::plan_one(&v, &remote_only, "dev1", ConflictPolicy::Prompt, &dav);
    assert_eq!(e.action, PlanAction::Pull);
    assert!(e.local_hash.is_none());
    assert!(engine::forced_push(remote_only).is_none());
    assert_eq!(engine::forced_push(s).unwrap().action, PlanAction::Push);
}

#[test]
fn pull_writes_own_head_and_never_a_blob_or_history() {
    let v = vault();
    let dav = MemoryDav::new();
    let old = save("G", "old", T1);
    push_one(&v, &old, "dev1", &dav).unwrap();
    push_one(&v, &with_content(&old, "new", T2), "dev2", &dav).unwrap();
    let plan = engine::plan(
        &v,
        std::slice::from_ref(&old),
        "dev1",
        ConflictPolicy::Prompt,
        &dav,
    );
    let pulled = plan[0].pull_hash.clone().unwrap();
    let before: Vec<String> = dav
        .keys()
        .into_iter()
        .filter(|k| !k.ends_with("/heads/dev1.json"))
        .collect();

    let io = RecIo::default();
    let sink = RecSink::default();
    let summary = engine::execute(&v, &plan, "dev1", &dav, &io, &sink);

    assert_eq!(summary.pulled, 1);
    let after: Vec<String> = dav
        .keys()
        .into_iter()
        .filter(|k| !k.ends_with("/heads/dev1.json"))
        .collect();
    assert_eq!(before, after, "a pull must not upload blobs or history");
    let head = decrypt_head(&v, &dav, &old, "dev1");
    assert_eq!(head.hash, pulled);
    assert_eq!(head.mtime, T2);
    let restored = io.restored.lock().unwrap_or_else(PoisonError::into_inner);
    let files = restored[0].2.clone();
    let mut now_local = old.clone();
    now_local.files = files;
    now_local.mtime = T2.into();
    assert_eq!(
        engine::plan_one(&v, &now_local, "dev1", ConflictPolicy::Prompt, &dav).action,
        PlanAction::InSync
    );
    assert_eq!(
        *sink.events.lock().unwrap_or_else(PoisonError::into_inner),
        vec![
            "started 1".to_string(),
            "0/1 G Pulling".into(),
            "0/1 G Restoring".into()
        ]
    );
}

#[test]
fn in_sync_with_missing_own_head_repairs_the_base() {
    let v = vault();
    let dav = MemoryDav::new();
    let s = save("G", "same", T1);
    push_one(&v, &s, "dev2", &dav).unwrap();
    let plan = engine::plan(
        &v,
        std::slice::from_ref(&s),
        "dev1",
        ConflictPolicy::Prompt,
        &dav,
    );
    assert_eq!(plan[0].action, PlanAction::InSync);
    let summary = engine::execute(
        &v,
        &plan,
        "dev1",
        &dav,
        &RecIo::default(),
        &RecSink::default(),
    );
    assert_eq!(summary.in_sync, 1);
    assert_eq!(
        decrypt_head(&v, &dav, &s, "dev1").hash,
        plan[0].local_hash.clone().unwrap()
    );
}

#[test]
fn failed_restore_is_counted_and_the_run_continues() {
    let v = vault();
    let dav = MemoryDav::new();
    let old = save("G", "old", T1);
    push_one(&v, &old, "dev1", &dav).unwrap();
    push_one(&v, &with_content(&old, "new", T2), "dev2", &dav).unwrap();
    let fresh = save("FRESH", "x", T1);
    let plan = engine::plan(&v, &[old, fresh], "dev1", ConflictPolicy::Prompt, &dav);
    let io = RecIo::default();
    io.fail_first.store(true, Ordering::SeqCst);
    let s = engine::execute(&v, &plan, "dev1", &dav, &io, &RecSink::default());
    assert_eq!((s.errors, s.pushed, s.pulled), (1, 1, 0));
    assert_eq!(s.error_messages, vec!["G: restore aborted".to_string()]);
}

struct ErrIo;
impl SaveIo for ErrIo {
    fn restore(
        &self,
        _: usize,
        _: &NormalizedSave,
        _: Vec<(String, Vec<u8>)>,
    ) -> Result<bool, String> {
        Err("disk full".into())
    }
}

#[test]
fn pull_hash_types_fetch_and_write_failures_and_keeps_the_base() {
    let v = vault();
    let dav = MemoryDav::new();
    let s = save("G", "x", T1);
    push_one(&v, &s, "dev2", &dav).unwrap();
    let hash = decrypt_head(&v, &dav, &s, "dev2").hash;

    let missing = engine::pull_hash(&v, 0, &s, "0000", T1, "dev1", &dav, &RecIo::default());
    assert!(matches!(
        missing,
        Err(RestoreError::Fetch(SyncError::BlobNotFound(_)))
    ));
    let write = engine::pull_hash(&v, 0, &s, &hash, T1, "dev1", &dav, &ErrIo);
    assert!(matches!(write, Err(RestoreError::Write(ref m)) if m == "disk full"));
    let own_head = format!("{}/heads/dev1.json", remote_path(&v, &s));
    assert!(
        dav.read(&own_head).is_none(),
        "a failed pull must not write this device's head"
    );
}

#[test]
fn cancel_stops_before_the_next_item() {
    let v = vault();
    let dav = MemoryDav::new();
    let plan: Vec<_> = ["A", "B", "C"]
        .iter()
        .map(|k| engine::forced_push(save(k, k, T1)).unwrap())
        .collect();
    let sink = RecSink {
        cancel_after: Some(1),
        ..Default::default()
    };
    let s = engine::execute(&v, &plan, "dev1", &dav, &RecIo::default(), &sink);
    assert!(s.cancelled);
    assert_eq!(s.pushed, 1);
}

#[test]
fn execute_refuses_to_push_a_remote_only_save() {
    let v = vault();
    let dav = MemoryDav::new();
    let mut entry = engine::forced_push(save("G", "x", T1)).unwrap();
    entry.save.files.clear();
    let s = engine::execute(
        &v,
        &[entry],
        "dev1",
        &dav,
        &RecIo::default(),
        &RecSink::default(),
    );
    assert_eq!((s.errors, s.pushed), (1, 0));
    assert!(dav.keys().is_empty(), "nothing may be uploaded");
}

#[test]
fn push_with_unknown_mtime_stamps_now() {
    let v = vault();
    let dav = MemoryDav::new();
    let s = save("G", "x", "");
    push_one(&v, &s, "dev1", &dav).unwrap();
    assert!(!decrypt_head(&v, &dav, &s, "dev1").mtime.is_empty());
}

#[test]
fn headline_mirrors_shell_common() {
    assert_eq!(
        SyncSummary::default().headline(),
        "Done \u{2014} nothing to sync"
    );
    let s = SyncSummary {
        pushed: 2,
        pulled: 1,
        in_sync: 3,
        conflicts: 1,
        errors: 1,
        ..Default::default()
    };
    assert_eq!(
        s.headline(),
        "Done \u{2014} 3 synced, 2 uploaded, 1 downloaded, 1 conflict, 1 failed"
    );
    let c = SyncSummary {
        conflicts: 2,
        cancelled: true,
        ..Default::default()
    };
    assert_eq!(c.headline(), "Cancelled \u{2014} 2 conflicts");
}

#[test]
fn remote_only_lists_server_slots_missing_locally() {
    let v = vault();
    let dav = MemoryDav::new();
    let mk = |slot: &str| {
        let mut s = save("x", "rom-save", T1);
        s.id.system = SystemId::Nds;
        s.id.source = "rom_keyed".into();
        s.id.game.key = "AMCE-1A2B".into();
        s.id.slot = slot.into();
        s.id.kind = SaveKind::Battery;
        s.group_key = build_group_key(SystemId::Nds, "AMCE-1A2B", slot);
        s
    };
    push_one(&v, &mk("battery"), "dev2", &dav).unwrap();
    push_one(&v, &mk("slot-1"), "dev2", &dav).unwrap();
    let roms = [RomRef {
        system: SystemId::Nds,
        rom_id: "AMCE-1A2B".into(),
        display_name: "Mario Kart DS".into(),
    }];
    let local: std::collections::BTreeSet<String> = ["nds/AMCE-1A2B/battery".to_string()].into();
    let found = remote_only_saves(&v, &roms, &local, &dav);
    assert_eq!(found.len(), 1);
    assert_eq!(found[0].group_key, "nds/AMCE-1A2B/slot-1");
    assert!(found[0].files.is_empty());
    assert_eq!(found[0].id.game.display_name, "Mario Kart DS");
}
