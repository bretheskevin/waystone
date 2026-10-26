use crate::config::SyncTarget;
use crate::helpers;
use crate::save_io::DesktopSaveIo;
use anyhow::Result;
use std::collections::BTreeSet;
use std::path::PathBuf;
use waystone_core::conflict::ConflictPolicy;
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_sync::WebDav;
use waystone_sync::engine::{
    self, MAX_ERROR_MESSAGES, PlanEntry, SyncPhase, SyncProgressSink, SyncSummary,
};
use waystone_sync::remote_only::{RomRef, remote_only_saves};

pub fn phase_label(phase: SyncPhase) -> &'static str {
    match phase {
        SyncPhase::Pushing => "Uploading",
        SyncPhase::Pulling => "Downloading",
        SyncPhase::Restoring => "Restoring",
    }
}

pub struct NoProgress;

impl SyncProgressSink for NoProgress {
    fn started(&self, _: usize) {}
    fn item(&self, _: usize, _: usize, _: &NormalizedSave, _: SyncPhase) {}
    fn is_cancelled(&self) -> bool {
        false
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Selection {
    All,
    Target(usize),
    Save { target: usize, group_key: String },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RunMode {
    Sync,
    Push,
    Pull,
}

/// Remote-only ROM slots can only be pulled, so a forced push never probes for them.
pub fn wants_remote_only(mode: RunMode) -> bool {
    mode != RunMode::Push
}

#[derive(Debug, Default)]
pub struct Collected {
    /// (target index, save)
    pub saves: Vec<(usize, NormalizedSave)>,
    pub scan_errors: Vec<String>,
}

pub struct PlannedRun {
    pub entries: Vec<PlanEntry>,
    /// plan index -> target index
    pub owners: Vec<usize>,
    pub scan_errors: Vec<String>,
}

fn selected_targets(sel: &Selection, count: usize) -> Vec<usize> {
    match sel {
        Selection::All => (0..count).collect(),
        Selection::Target(i) | Selection::Save { target: i, .. } => vec![*i],
    }
}

fn remote_only_for(
    target: &SyncTarget,
    local: &[NormalizedSave],
    vault: &Vault,
    dav: &dyn WebDav,
) -> Vec<NormalizedSave> {
    let roms = match helpers::scan_target_roms(target) {
        Ok(r) => r,
        Err(e) => {
            log::warn!("[sync] remote-only scan {} failed: {e:#}", target.name);
            return Vec::new();
        }
    };
    let refs: Vec<RomRef> = roms
        .into_iter()
        .map(|r| RomRef {
            system: r.system,
            rom_id: r.rom_id,
            display_name: r.display_name,
        })
        .collect();
    let keys: BTreeSet<String> = local.iter().map(|s| s.group_key.clone()).collect();
    remote_only_saves(vault, &refs, &keys, dav)
}

/// Local saves (+ remote-only ROM slots when `remote` is given) per selected target.
pub fn collect_saves(
    targets: &[SyncTarget],
    sel: &Selection,
    remote: Option<(&Vault, &dyn WebDav)>,
) -> Result<Collected> {
    let mut out = Collected::default();
    for idx in selected_targets(sel, targets.len()) {
        let target = targets
            .get(idx)
            .ok_or_else(|| anyhow::anyhow!("no target at index {idx}"))?;
        let mut saves = match helpers::load_target_saves(target) {
            Ok(s) => s,
            Err(e) if matches!(sel, Selection::Save { .. }) => return Err(e),
            Err(e) => {
                log::warn!("[sync] target {} scan failed: {e:#}", target.name);
                out.scan_errors.push(format!("{}: {e:#}", target.name));
                continue;
            }
        };
        if let (Some((vault, dav)), true) = (remote, helpers::is_rom_keyed(&target.adapter)) {
            let extra = remote_only_for(target, &saves, vault, dav);
            saves.extend(extra);
        }
        if let Selection::Save { group_key, .. } = sel {
            saves.retain(|s| &s.group_key == group_key);
        }
        log::info!(
            "[sync] target {}: {} save(s) collected",
            target.name,
            saves.len()
        );
        out.saves.extend(saves.into_iter().map(|s| (idx, s)));
    }
    Ok(out)
}

pub fn plan_run(
    vault: &Vault,
    collected: Collected,
    device_id: &str,
    policy: ConflictPolicy,
    mode: RunMode,
    dav: &dyn WebDav,
) -> PlannedRun {
    let Collected { saves, scan_errors } = collected;
    let (owners, saves): (Vec<usize>, Vec<NormalizedSave>) = saves.into_iter().unzip();
    let (owners, entries) = match mode {
        RunMode::Sync => (owners, engine::plan(vault, &saves, device_id, policy, dav)),
        RunMode::Push => {
            let mut o = Vec::new();
            let mut e = Vec::new();
            for (owner, save) in owners.into_iter().zip(saves) {
                if let Some(entry) = engine::forced_push(save) {
                    o.push(owner);
                    e.push(entry);
                }
            }
            (o, e)
        }
        RunMode::Pull => {
            let (o, e): (Vec<usize>, Vec<PlanEntry>) = owners
                .into_iter()
                .zip(engine::plan(vault, &saves, device_id, policy, dav))
                .filter(|(_, e)| e.action == engine::PlanAction::Pull)
                .unzip();
            (o, e)
        }
    };
    PlannedRun {
        entries,
        owners,
        scan_errors,
    }
}

#[allow(clippy::too_many_arguments)]
pub fn execute_run(
    vault: &Vault,
    run: &PlannedRun,
    targets: &[SyncTarget],
    device_id: &str,
    safety_backup: bool,
    backups_root: PathBuf,
    dav: &dyn WebDav,
    progress: &dyn SyncProgressSink,
) -> SyncSummary {
    let io = DesktopSaveIo {
        targets,
        owners: &run.owners,
        safety_backup,
        backups_root,
    };
    let mut summary = engine::execute(vault, &run.entries, device_id, dav, &io, progress);
    for e in &run.scan_errors {
        summary.errors += 1;
        if summary.error_messages.len() < MAX_ERROR_MESSAGES {
            summary.error_messages.push(e.clone());
        }
    }
    summary
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;
    use waystone_sync::testing::MemoryDav;

    fn vault() -> Vault {
        Vault::init("sync-run-tests").unwrap().0
    }

    fn target(dir: &Path) -> SyncTarget {
        SyncTarget::plain("GBA", dir, "mgba", "gba")
    }

    fn run(
        v: &Vault,
        dav: &MemoryDav,
        device: &str,
        targets: &[SyncTarget],
        sel: Selection,
        mode: RunMode,
    ) -> (PlannedRun, SyncSummary) {
        let collected = collect_saves(targets, &sel, None).unwrap();
        let planned = plan_run(v, collected, device, ConflictPolicy::NewestWins, mode, dav);
        let backups = tempfile::tempdir().unwrap();
        let summary = execute_run(
            v,
            &planned,
            targets,
            device,
            true,
            backups.path().to_path_buf(),
            dav,
            &NoProgress,
        );
        (planned, summary)
    }

    #[test]
    fn sync_pushes_a_fresh_local_save() {
        let (v, dav) = (vault(), MemoryDav::new());
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("Game.sav"), b"v1").unwrap();
        let (planned, s) = run(
            &v,
            &dav,
            "dev-a",
            &[target(dir.path())],
            Selection::All,
            RunMode::Sync,
        );
        assert_eq!(planned.entries.len(), 1);
        assert_eq!((s.pushed, s.errors), (1, 0), "{s:?}");
        assert!(dav.keys().iter().any(|k| k.ends_with("heads/dev-a.json")));
    }

    #[test]
    fn pull_restores_newer_remote_and_writes_own_head() {
        let (v, dav) = (vault(), MemoryDav::new());
        let (a, b) = (tempfile::tempdir().unwrap(), tempfile::tempdir().unwrap());
        for d in [&a, &b] {
            std::fs::write(d.path().join("Game.sav"), b"v1").unwrap();
        }
        run(
            &v,
            &dav,
            "dev-a",
            &[target(a.path())],
            Selection::All,
            RunMode::Sync,
        );
        run(
            &v,
            &dav,
            "dev-b",
            &[target(b.path())],
            Selection::All,
            RunMode::Push,
        );
        std::fs::write(a.path().join("Game.sav"), b"v2-newer").unwrap();
        let mut newer = helpers::load_target_saves(&target(a.path())).unwrap();
        newer[0].mtime = "2030-01-01T00:00:00Z".into();
        waystone_sync::push_one(&v, &newer[0], "dev-a", &dav).unwrap();

        let (planned, s) = run(
            &v,
            &dav,
            "dev-b",
            &[target(b.path())],
            Selection::All,
            RunMode::Pull,
        );
        assert_eq!(planned.entries.len(), 1);
        assert_eq!((s.pulled, s.errors), (1, 0), "{s:?}");
        assert_eq!(
            std::fs::read(b.path().join("Game.sav")).unwrap(),
            b"v2-newer"
        );
        let heads = waystone_sync::read_remote_heads(&v, &newer[0], &dav).unwrap();
        let head = |d: &str| {
            heads
                .iter()
                .find(|h| h.device_id == d)
                .unwrap()
                .hash
                .clone()
        };
        assert_eq!(head("dev-b"), head("dev-a"));
    }

    #[test]
    fn push_skips_remote_only_saves() {
        let v = vault();
        let remote_only = NormalizedSave {
            files: Vec::new(),
            ..sample_save("gba/ABCD/battery")
        };
        let collected = Collected {
            saves: vec![(0, remote_only)],
            scan_errors: vec![],
        };
        let planned = plan_run(
            &v,
            collected,
            "dev",
            ConflictPolicy::NewestWins,
            RunMode::Push,
            &MemoryDav::new(),
        );
        assert!(planned.entries.is_empty() && planned.owners.is_empty());
    }

    fn sample_save(group_key: &str) -> NormalizedSave {
        use waystone_core::model::*;
        NormalizedSave {
            id: SaveId {
                source: "mgba".into(),
                system: SystemId::Gba,
                game: GameRef {
                    key: "ABCD".into(),
                    display_name: "g".into(),
                    confidence: Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: "battery".into(),
                kind: SaveKind::Battery,
            },
            group_key: group_key.into(),
            portable: true,
            mtime: String::new(),
            files: vec![("battery".into(), b"x".to_vec())],
        }
    }

    #[test]
    fn selection_save_limits_plan_to_one_entry() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("A.sav"), b"a").unwrap();
        std::fs::write(dir.path().join("B.sav"), b"b").unwrap();
        let targets = [target(dir.path())];
        let all = collect_saves(&targets, &Selection::All, None).unwrap();
        assert_eq!(all.saves.len(), 2);
        let key = all.saves[0].1.group_key.clone();
        let one = collect_saves(
            &targets,
            &Selection::Save {
                target: 0,
                group_key: key.clone(),
            },
            None,
        )
        .unwrap();
        assert_eq!(one.saves.len(), 1);
        assert_eq!(one.saves[0].1.group_key, key);
    }

    #[test]
    fn scan_failure_is_collected_for_all_but_fatal_for_save() {
        let bad = SyncTarget::plain("bad", Path::new("/nonexistent"), "bogus", "gba");
        let targets = [bad];
        let c = collect_saves(&targets, &Selection::All, None).unwrap();
        assert_eq!(c.scan_errors.len(), 1);
        assert!(c.scan_errors[0].starts_with("bad: "));
        let sel = Selection::Save {
            target: 0,
            group_key: "x".into(),
        };
        assert!(collect_saves(&targets, &sel, None).is_err());

        let planned = plan_run(
            &vault(),
            c,
            "dev",
            ConflictPolicy::NewestWins,
            RunMode::Sync,
            &MemoryDav::new(),
        );
        let s = execute_run(
            &vault(),
            &planned,
            &targets,
            "dev",
            false,
            PathBuf::new(),
            &MemoryDav::new(),
            &NoProgress,
        );
        assert_eq!(s.errors, 1);
        assert_eq!(s.error_messages.len(), 1);
    }

    fn decide(local_secs: u64, remote_mtime: &str) -> engine::PlanAction {
        let (v, dav) = (vault(), MemoryDav::new());
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("Game.sav");
        std::fs::write(&file, b"local").unwrap();
        std::fs::File::options()
            .write(true)
            .open(&file)
            .unwrap()
            .set_modified(std::time::UNIX_EPOCH + std::time::Duration::from_secs(local_secs))
            .unwrap();
        let targets = [target(dir.path())];
        let mut remote = helpers::load_target_saves(&targets[0]).unwrap();
        remote[0].files = vec![("battery".into(), b"remote".to_vec())];
        remote[0].mtime = remote_mtime.into();
        waystone_sync::push_one(&v, &remote[0], "dev-other", &dav).unwrap();
        let collected = collect_saves(&targets, &Selection::All, None).unwrap();
        plan_run(
            &v,
            collected,
            "dev-me",
            ConflictPolicy::NewestWins,
            RunMode::Sync,
            &dav,
        )
        .entries[0]
            .action
    }

    #[test]
    fn newest_wins_uses_local_file_mtime() {
        // local 2023-11-14, remote 2020 -> local newer -> push
        assert_eq!(
            decide(1_700_000_000, "2020-01-01T00:00:00Z"),
            engine::PlanAction::Push
        );
        // local 2023-11-14, remote 2030 -> remote newer -> pull
        assert_eq!(
            decide(1_700_000_000, "2030-01-01T00:00:00Z"),
            engine::PlanAction::Pull
        );
    }
}
