//! Decide-first sync engine shared by desktop and Android (semantics of shell-common/sync_engine.cpp).
use crate::orchestration::{
    encode_head, fetch_blob, mtime_or_now, push_one, read_remote_heads, remote_path,
};
use crate::{Result, SyncError, WebDav};
use std::time::Instant;
use waystone_core::conflict::{self, ConflictPolicy, ConflictWinner, DeviceHead, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_core::packaging;

pub const MAX_ERROR_MESSAGES: usize = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlanAction {
    Push,
    Pull,
    InSync,
    Conflict,
    Error,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SyncPhase {
    Pushing,
    Pulling,
    Restoring,
}

#[derive(Debug, Clone)]
pub struct PlanEntry {
    pub save: NormalizedSave,
    pub action: PlanAction,
    pub decision: Option<SyncDecision>,
    pub local_hash: Option<String>,
    pub own_head_hash: Option<String>,
    pub pull_hash: Option<String>,
    /// Merged (newest) head mtime; becomes this device's base mtime after a pull.
    pub head_mtime: String,
    pub heads: Vec<DeviceHead>,
    pub error: Option<String>,
}

impl PlanEntry {
    fn new(save: NormalizedSave, action: PlanAction) -> Self {
        Self {
            save,
            action,
            decision: None,
            local_hash: None,
            own_head_hash: None,
            pull_hash: None,
            head_mtime: String::new(),
            heads: Vec::new(),
            error: None,
        }
    }

    fn failed(save: NormalizedSave, why: String) -> Self {
        let mut e = Self::new(save, PlanAction::Error);
        e.error = Some(why);
        e
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SyncSummary {
    pub pushed: usize,
    pub pulled: usize,
    pub in_sync: usize,
    pub conflicts: usize,
    pub errors: usize,
    pub error_messages: Vec<String>,
    pub cancelled: bool,
}

impl SyncSummary {
    /// Mirrors shell-common `format_sync_headline` (zero parts omitted).
    pub fn headline(&self) -> String {
        let conflict_label = if self.conflicts == 1 {
            "conflict"
        } else {
            "conflicts"
        };
        let parts: Vec<String> = [
            (self.in_sync, "synced"),
            (self.pushed, "uploaded"),
            (self.pulled, "downloaded"),
            (self.conflicts, conflict_label),
            (self.errors, "failed"),
        ]
        .iter()
        .filter(|(n, _)| *n > 0)
        .map(|(n, label)| format!("{n} {label}"))
        .collect();
        let body = if parts.is_empty() {
            "nothing to sync".to_string()
        } else {
            parts.join(", ")
        };
        let lead = if self.cancelled { "Cancelled" } else { "Done" };
        format!("{lead} \u{2014} {body}")
    }
}

/// Why a pull / history restore failed: fetching the remote blob vs writing it locally.
#[derive(Debug)]
pub enum RestoreError {
    Fetch(SyncError),
    Write(String),
}

impl std::fmt::Display for RestoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Fetch(e) => write!(f, "{e}"),
            Self::Write(msg) => write!(f, "{msg}"),
        }
    }
}

impl std::error::Error for RestoreError {}

/// Writes pulled files for plan entry `index`. Ok(false) = aborted (e.g. safety snapshot failed).
pub trait SaveIo {
    fn restore(
        &self,
        index: usize,
        save: &NormalizedSave,
        files: Vec<(String, Vec<u8>)>,
    ) -> std::result::Result<bool, String>;
}

pub trait SyncProgressSink {
    fn started(&self, total: usize);
    fn item(&self, index: usize, total: usize, save: &NormalizedSave, phase: SyncPhase);
    /// Checked before each item: a cancel takes effect after the current save.
    fn is_cancelled(&self) -> bool;
}

/// A ROM slot that exists only on the server: restored, never pushed.
pub fn is_remote_only(save: &NormalizedSave) -> bool {
    save.files.is_empty()
}

/// Unconditional push (single-save "Push" / conflict keep-local); None for a remote-only save.
pub fn forced_push(save: NormalizedSave) -> Option<PlanEntry> {
    if is_remote_only(&save) {
        log::warn!(
            "[sync] forced push refused for remote-only {}",
            save.group_key
        );
        return None;
    }
    Some(PlanEntry::new(save, PlanAction::Push))
}

fn effective_policy(policy: ConflictPolicy, local_mtime: &str, has_local: bool) -> ConflictPolicy {
    if policy == ConflictPolicy::NewestWins && has_local && local_mtime.is_empty() {
        ConflictPolicy::Prompt
    } else {
        policy
    }
}

pub fn plan_one(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    policy: ConflictPolicy,
    dav: &dyn WebDav,
) -> PlanEntry {
    let t0 = Instant::now();
    match try_plan_one(vault, save, device_id, policy, dav) {
        Ok(e) => {
            log::info!(
                "[sync] plan {}: {:?} (decision={:?} remote_only={} local={:.12} own={:.12} head_mtime={}) in {} ms",
                save.group_key,
                e.action,
                e.decision,
                is_remote_only(save),
                e.local_hash.as_deref().unwrap_or("-"),
                e.own_head_hash.as_deref().unwrap_or("-"),
                e.head_mtime,
                t0.elapsed().as_millis()
            );
            e
        }
        Err(err) => {
            log::warn!("[sync] plan {} FAILED: {err}", save.group_key);
            PlanEntry::failed(save.clone(), err.to_string())
        }
    }
}

fn try_plan_one(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    policy: ConflictPolicy,
    dav: &dyn WebDav,
) -> Result<PlanEntry> {
    let remote_only = is_remote_only(save);
    let (local_hash, local_mtime) = if remote_only {
        (None, String::new())
    } else {
        let (entry, _) = packaging::package(save);
        (Some(entry.content.hash), entry.mtime)
    };
    let heads = read_remote_heads(vault, save, dav)?;
    let pol = effective_policy(policy, &local_mtime, local_hash.is_some());
    if pol != policy {
        log::info!(
            "[sync] {}: unknown local mtime -> newest-wins treated as prompt",
            save.group_key
        );
    }
    let decision =
        conflict::decide_pull(local_hash.as_deref(), &local_mtime, &heads, device_id, pol);
    let merged = conflict::fold_heads(&heads);
    let mut e = PlanEntry::new(save.clone(), PlanAction::InSync);
    e.head_mtime = merged.as_ref().map(|m| m.mtime.clone()).unwrap_or_default();
    e.own_head_hash = heads
        .iter()
        .find(|h| h.device_id == device_id)
        .map(|h| h.hash.clone());
    e.local_hash = local_hash;
    match &decision {
        SyncDecision::InSync => e.action = PlanAction::InSync,
        SyncDecision::Push if remote_only => {
            e.action = PlanAction::Error;
            e.error = Some("remote-only save cannot be pushed".into());
        }
        SyncDecision::Push
        | SyncDecision::ConflictResolved {
            winner: ConflictWinner::Local,
            ..
        } => e.action = PlanAction::Push,
        SyncDecision::Pull { head_hash } => {
            e.action = PlanAction::Pull;
            e.pull_hash = Some(head_hash.clone());
        }
        SyncDecision::ConflictResolved {
            winner: ConflictWinner::Remote,
            ..
        } => match merged {
            Some(m) => {
                e.action = PlanAction::Pull;
                e.pull_hash = Some(m.hash);
            }
            None => {
                e.action = PlanAction::Error;
                e.error = Some("no remote head to pull".into());
            }
        },
        SyncDecision::ConflictNeedsInput { .. } => e.action = PlanAction::Conflict,
    }
    e.decision = Some(decision);
    e.heads = heads;
    Ok(e)
}

pub fn plan(
    vault: &Vault,
    saves: &[NormalizedSave],
    device_id: &str,
    policy: ConflictPolicy,
    dav: &dyn WebDav,
) -> Vec<PlanEntry> {
    let t0 = Instant::now();
    log::info!(
        "[sync] plan start: {} save(s), policy={policy:?}",
        saves.len()
    );
    let entries: Vec<PlanEntry> = saves
        .iter()
        .map(|s| plan_one(vault, s, device_id, policy, dav))
        .collect();
    log::info!(
        "[sync] plan done in {} ms: {}",
        t0.elapsed().as_millis(),
        describe(&entries)
    );
    entries
}

pub fn describe(entries: &[PlanEntry]) -> String {
    describe_actions(&entries.iter().map(|e| e.action).collect::<Vec<_>>())
}

/// "push=N pull=N insync=N conflict=N error=N".
pub fn describe_actions(actions: &[PlanAction]) -> String {
    [
        (PlanAction::Push, "push"),
        (PlanAction::Pull, "pull"),
        (PlanAction::InSync, "insync"),
        (PlanAction::Conflict, "conflict"),
        (PlanAction::Error, "error"),
    ]
    .iter()
    .map(|(a, n)| format!("{n}={}", actions.iter().filter(|x| *x == a).count()))
    .collect::<Vec<_>>()
    .join(" ")
}

/// heads/<device_id>.json = {device_id, hash, mtime}; never a blob or history entry.
pub fn write_own_head(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    hash: &str,
    mtime: &str,
    dav: &dyn WebDav,
) -> Result<()> {
    let heads = format!("{}/heads", remote_path(vault, save));
    dav.mkdir_p(&heads)?;
    let head = DeviceHead {
        device_id: device_id.to_string(),
        hash: hash.to_string(),
        mtime: mtime.to_string(),
    };
    let (_, encrypted) = encode_head(vault, &head)?;
    dav.put(&format!("{heads}/{device_id}.json"), encrypted)?;
    log::info!("[sync] head {heads}/{device_id}.json -> hash={hash:.12} mtime={mtime}");
    Ok(())
}

fn fetch_files(
    vault: &Vault,
    save: &NormalizedSave,
    hash: &str,
    dav: &dyn WebDav,
) -> Result<Vec<(String, Vec<u8>)>> {
    let zip = fetch_blob(vault, save, hash, dav)?;
    let files = packaging::unzip(&zip)?;
    log::info!(
        "[sync] fetched {} blob {hash:.12}: {} file(s), {} bytes",
        save.group_key,
        files.len(),
        zip.len()
    );
    Ok(files)
}

fn update_base(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    hash: &str,
    head_mtime: &str,
    dav: &dyn WebDav,
) {
    let mtime = mtime_or_now(head_mtime);
    if let Err(e) = write_own_head(vault, save, device_id, hash, &mtime, dav) {
        log::warn!(
            "[sync] base update failed for {} ({e}) -- healed by the next in-sync pass",
            save.group_key
        );
    }
}

/// History restore: write `hash` through `io` WITHOUT touching this device's head.
pub fn restore_hash(
    vault: &Vault,
    index: usize,
    save: &NormalizedSave,
    hash: &str,
    dav: &dyn WebDav,
    io: &dyn SaveIo,
) -> std::result::Result<bool, RestoreError> {
    fetch_and_restore(vault, index, save, hash, dav, io, &|| {})
}

fn fetch_and_restore(
    vault: &Vault,
    index: usize,
    save: &NormalizedSave,
    hash: &str,
    dav: &dyn WebDav,
    io: &dyn SaveIo,
    before_write: &dyn Fn(),
) -> std::result::Result<bool, RestoreError> {
    let result = fetch_files(vault, save, hash, dav)
        .map_err(RestoreError::Fetch)
        .and_then(|files| {
            before_write();
            io.restore(index, save, files).map_err(RestoreError::Write)
        });
    match &result {
        Ok(ok) => log::info!(
            "[sync] restore {} {hash:.12}: {}",
            save.group_key,
            if *ok { "written" } else { "aborted" }
        ),
        Err(e) => log::warn!("[sync] restore {} {hash:.12} FAILED: {e:?}", save.group_key),
    }
    result
}

/// Pull / keep-remote: restore `hash`, then write this device's head = {hash, head_mtime}.
#[allow(clippy::too_many_arguments)]
pub fn pull_hash(
    vault: &Vault,
    index: usize,
    save: &NormalizedSave,
    hash: &str,
    head_mtime: &str,
    device_id: &str,
    dav: &dyn WebDav,
    io: &dyn SaveIo,
) -> std::result::Result<bool, RestoreError> {
    pull_with(
        vault,
        index,
        save,
        hash,
        head_mtime,
        device_id,
        dav,
        io,
        &|| {},
    )
}

#[allow(clippy::too_many_arguments)]
fn pull_with(
    vault: &Vault,
    index: usize,
    save: &NormalizedSave,
    hash: &str,
    head_mtime: &str,
    device_id: &str,
    dav: &dyn WebDav,
    io: &dyn SaveIo,
    before_write: &dyn Fn(),
) -> std::result::Result<bool, RestoreError> {
    let ok = fetch_and_restore(vault, index, save, hash, dav, io, before_write)?;
    if ok {
        update_base(vault, save, device_id, hash, head_mtime, dav);
    }
    Ok(ok)
}

/// Acts on a decided plan; never aborts — every failure lands in the summary.
pub fn execute(
    vault: &Vault,
    plan: &[PlanEntry],
    device_id: &str,
    dav: &dyn WebDav,
    io: &dyn SaveIo,
    progress: &dyn SyncProgressSink,
) -> SyncSummary {
    let t0 = Instant::now();
    let total = plan.len();
    progress.started(total);
    log::info!(
        "[sync] execute start: {total} entr(ies) ({})",
        describe(plan)
    );
    let mut s = SyncSummary::default();
    let mut errors: Vec<String> = Vec::new();
    let mut fail = |s: &mut SyncSummary, e: &PlanEntry, why: &str| {
        log::warn!("[sync] execute {} FAILED: {why}", e.save.group_key);
        errors.push(format!("{}: {why}", e.save.id.game.display_name));
        s.errors += 1;
    };
    for (index, e) in plan.iter().enumerate() {
        if progress.is_cancelled() {
            log::info!("[sync] execute cancelled before item {}/{total}", index + 1);
            s.cancelled = true;
            break;
        }
        match e.action {
            PlanAction::InSync => {
                s.in_sync += 1;
                if let Some(local) = e.local_hash.as_deref()
                    && e.own_head_hash.as_deref() != Some(local)
                    && !e.head_mtime.is_empty()
                {
                    log::info!(
                        "[sync] {}: base stale (own={:.12}) -- repairing",
                        e.save.group_key,
                        e.own_head_hash.as_deref().unwrap_or("-")
                    );
                    if let Err(err) =
                        write_own_head(vault, &e.save, device_id, local, &e.head_mtime, dav)
                    {
                        log::warn!(
                            "[sync] base repair failed for {} ({err}, non-fatal)",
                            e.save.group_key
                        );
                    }
                }
            }
            PlanAction::Conflict => {
                log::info!(
                    "[sync] {}: conflict -- skipped (resolve in Conflicts)",
                    e.save.group_key
                );
                s.conflicts += 1;
            }
            PlanAction::Error => fail(&mut s, e, e.error.as_deref().unwrap_or("unknown error")),
            PlanAction::Push if is_remote_only(&e.save) => {
                fail(&mut s, e, "remote-only save cannot be pushed")
            }
            PlanAction::Push => {
                progress.item(index, total, &e.save, SyncPhase::Pushing);
                match push_one(vault, &e.save, device_id, dav) {
                    Ok(o) => {
                        log::info!("[sync] push {} -> {o:?}", e.save.group_key);
                        s.pushed += 1;
                    }
                    Err(err) => fail(&mut s, e, &err.to_string()),
                }
            }
            PlanAction::Pull => {
                let Some(hash) = e.pull_hash.as_deref() else {
                    fail(&mut s, e, "no head hash");
                    continue;
                };
                progress.item(index, total, &e.save, SyncPhase::Pulling);
                let restoring = || progress.item(index, total, &e.save, SyncPhase::Restoring);
                match pull_with(
                    vault,
                    index,
                    &e.save,
                    hash,
                    &e.head_mtime,
                    device_id,
                    dav,
                    io,
                    &restoring,
                ) {
                    Ok(true) => s.pulled += 1,
                    Ok(false) => fail(&mut s, e, "restore aborted"),
                    Err(err) => fail(&mut s, e, &err.to_string()),
                }
            }
        }
    }
    s.error_messages = errors.into_iter().take(MAX_ERROR_MESSAGES).collect();
    log::info!(
        "[sync] execute done in {} ms: {}",
        t0.elapsed().as_millis(),
        s.headline()
    );
    s
}
