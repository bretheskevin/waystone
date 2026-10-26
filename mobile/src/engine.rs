use crate::decision::{ConflictPolicy, SyncDecision};
use crate::error::WaystoneError;
use crate::exports::{
    from_core_decision, from_core_save, map_conflict_policy, parse_system, to_core_save,
};
use crate::types::{DeviceHead, FileEntry, NormalizedSave};
use crate::vault::Vault;
use crate::webdav::{WebDav, WebDavBridge};
use std::collections::BTreeSet;
use std::sync::Arc;
use waystone_core::conflict as core_conflict;
use waystone_sync::engine as eng;
use waystone_sync::remote_only as ro;

#[derive(Debug, Clone, Copy, PartialEq, Eq, uniffi::Enum)]
pub enum PlanAction {
    Push,
    Pull,
    InSync,
    Conflict,
    Error,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, uniffi::Enum)]
pub enum SyncPhase {
    Pushing,
    Pulling,
    Restoring,
}

#[derive(Debug, uniffi::Record)]
pub struct SyncPlanEntry {
    pub save: NormalizedSave,
    pub action: PlanAction,
    pub decision: Option<SyncDecision>,
    pub local_hash: Option<String>,
    pub own_head_hash: Option<String>,
    pub pull_hash: Option<String>,
    pub head_mtime: String,
    pub heads: Vec<DeviceHead>,
    pub error: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, uniffi::Record)]
pub struct SyncSummary {
    pub pushed: u32,
    pub pulled: u32,
    pub in_sync: u32,
    pub conflicts: u32,
    pub errors: u32,
    pub error_messages: Vec<String>,
    pub cancelled: bool,
}

#[derive(Debug, Clone, uniffi::Record)]
pub struct RomRef {
    pub system: String,
    pub rom_id: String,
    pub display_name: String,
}

#[uniffi::export(with_foreign)]
pub trait SaveIo: Send + Sync {
    /// Write `files` for plan entry `index`; false = aborted (safety snapshot failed).
    fn restore(
        &self,
        index: u32,
        save: NormalizedSave,
        files: Vec<FileEntry>,
    ) -> Result<bool, WaystoneError>;
}

/// Every method may fail: a sink error is logged and ignored so the UI can never abort a sync.
#[uniffi::export(with_foreign)]
pub trait SyncProgressSink: Send + Sync {
    fn started(&self, total: u32) -> Result<(), WaystoneError>;
    /// Only the save's identity crosses here: its bytes would be re-sent on every phase.
    fn item(
        &self,
        index: u32,
        total: u32,
        group_key: String,
        display_name: String,
        phase: SyncPhase,
    ) -> Result<(), WaystoneError>;
    fn is_cancelled(&self) -> Result<bool, WaystoneError>;
}

fn action_from_core(a: eng::PlanAction) -> PlanAction {
    match a {
        eng::PlanAction::Push => PlanAction::Push,
        eng::PlanAction::Pull => PlanAction::Pull,
        eng::PlanAction::InSync => PlanAction::InSync,
        eng::PlanAction::Conflict => PlanAction::Conflict,
        eng::PlanAction::Error => PlanAction::Error,
    }
}

fn action_to_core(a: PlanAction) -> eng::PlanAction {
    match a {
        PlanAction::Push => eng::PlanAction::Push,
        PlanAction::Pull => eng::PlanAction::Pull,
        PlanAction::InSync => eng::PlanAction::InSync,
        PlanAction::Conflict => eng::PlanAction::Conflict,
        PlanAction::Error => eng::PlanAction::Error,
    }
}

fn phase_from_core(p: eng::SyncPhase) -> SyncPhase {
    match p {
        eng::SyncPhase::Pushing => SyncPhase::Pushing,
        eng::SyncPhase::Pulling => SyncPhase::Pulling,
        eng::SyncPhase::Restoring => SyncPhase::Restoring,
    }
}

fn to_core_decision(d: &SyncDecision) -> core_conflict::SyncDecision {
    match d {
        SyncDecision::InSync => core_conflict::SyncDecision::InSync,
        SyncDecision::Push => core_conflict::SyncDecision::Push,
        SyncDecision::Pull { head_hash } => core_conflict::SyncDecision::Pull {
            head_hash: head_hash.clone(),
        },
        SyncDecision::ConflictResolved { winner, loser_hash } => {
            core_conflict::SyncDecision::ConflictResolved {
                winner: if winner == "remote" {
                    core_conflict::ConflictWinner::Remote
                } else {
                    core_conflict::ConflictWinner::Local
                },
                loser_hash: loser_hash.clone(),
            }
        }
        SyncDecision::ConflictNeedsInput {
            local_hash,
            remote_hash,
        } => core_conflict::SyncDecision::ConflictNeedsInput {
            local_hash: local_hash.clone(),
            remote_hash: remote_hash.clone(),
        },
    }
}

fn entry_from_core(e: eng::PlanEntry) -> SyncPlanEntry {
    SyncPlanEntry {
        save: from_core_save(&e.save),
        action: action_from_core(e.action),
        decision: e.decision.as_ref().map(from_core_decision),
        local_hash: e.local_hash,
        own_head_hash: e.own_head_hash,
        pull_hash: e.pull_hash,
        head_mtime: e.head_mtime,
        heads: e.heads.into_iter().map(DeviceHead::from).collect(),
        error: e.error,
    }
}

fn entry_to_core(e: &SyncPlanEntry) -> Result<eng::PlanEntry, WaystoneError> {
    Ok(eng::PlanEntry {
        save: to_core_save(&e.save)?,
        action: action_to_core(e.action),
        decision: e.decision.as_ref().map(to_core_decision),
        local_hash: e.local_hash.clone(),
        own_head_hash: e.own_head_hash.clone(),
        pull_hash: e.pull_hash.clone(),
        head_mtime: e.head_mtime.clone(),
        heads: e.heads.iter().cloned().map(Into::into).collect(),
        error: e.error.clone(),
    })
}

fn summary_to_core(s: &SyncSummary) -> eng::SyncSummary {
    eng::SyncSummary {
        pushed: s.pushed as usize,
        pulled: s.pulled as usize,
        in_sync: s.in_sync as usize,
        conflicts: s.conflicts as usize,
        errors: s.errors as usize,
        error_messages: s.error_messages.clone(),
        cancelled: s.cancelled,
    }
}

fn summary_from_core(s: eng::SyncSummary) -> SyncSummary {
    SyncSummary {
        pushed: s.pushed as u32,
        pulled: s.pulled as u32,
        in_sync: s.in_sync as u32,
        conflicts: s.conflicts as u32,
        errors: s.errors as u32,
        error_messages: s.error_messages,
        cancelled: s.cancelled,
    }
}

struct IoBridge(Arc<dyn SaveIo>);

impl eng::SaveIo for IoBridge {
    fn restore(
        &self,
        index: usize,
        save: &waystone_core::model::NormalizedSave,
        files: Vec<(String, Vec<u8>)>,
    ) -> Result<bool, String> {
        let files = files
            .into_iter()
            .map(|(path, content)| FileEntry { path, content })
            .collect();
        self.0
            .restore(index as u32, from_core_save(save), files)
            .map_err(|e| e.to_string())
    }
}

struct SinkBridge(Arc<dyn SyncProgressSink>);

fn sink_ok<T>(what: &str, r: Result<T, WaystoneError>, fallback: T) -> T {
    r.unwrap_or_else(|e| {
        log::warn!("[sync] progress sink {what} failed ({e}) -- ignored");
        fallback
    })
}

// A failing is_cancelled reads as "not cancelled": the engine is safe to run to completion,
// whereas treating a broken UI callback as a cancel would silently drop the user's sync.
impl eng::SyncProgressSink for SinkBridge {
    fn started(&self, total: usize) {
        sink_ok("started", self.0.started(total as u32), ())
    }
    fn item(
        &self,
        index: usize,
        total: usize,
        save: &waystone_core::model::NormalizedSave,
        phase: eng::SyncPhase,
    ) {
        let r = self.0.item(
            index as u32,
            total as u32,
            save.group_key.clone(),
            save.id.game.display_name.clone(),
            phase_from_core(phase),
        );
        sink_ok("item", r, ())
    }
    fn is_cancelled(&self) -> bool {
        sink_ok("is_cancelled", self.0.is_cancelled(), false)
    }
}

#[uniffi::export]
pub fn engine_plan_one(
    vault: Arc<Vault>,
    save: NormalizedSave,
    device_id: String,
    policy: ConflictPolicy,
    dav: Arc<dyn WebDav>,
) -> Result<SyncPlanEntry, WaystoneError> {
    let core = to_core_save(&save)?;
    let e = eng::plan_one(
        vault.core_vault(),
        &core,
        &device_id,
        map_conflict_policy(policy),
        &WebDavBridge { inner: dav },
    );
    Ok(entry_from_core(e))
}

#[uniffi::export]
pub fn engine_plan(
    vault: Arc<Vault>,
    saves: Vec<NormalizedSave>,
    device_id: String,
    policy: ConflictPolicy,
    dav: Arc<dyn WebDav>,
) -> Result<Vec<SyncPlanEntry>, WaystoneError> {
    let core = saves
        .iter()
        .map(to_core_save)
        .collect::<Result<Vec<_>, _>>()?;
    let plan = eng::plan(
        vault.core_vault(),
        &core,
        &device_id,
        map_conflict_policy(policy),
        &WebDavBridge { inner: dav },
    );
    Ok(plan.into_iter().map(entry_from_core).collect())
}

#[uniffi::export]
pub fn engine_forced_push(save: NormalizedSave) -> Result<Option<SyncPlanEntry>, WaystoneError> {
    Ok(eng::forced_push(to_core_save(&save)?).map(entry_from_core))
}

#[uniffi::export]
pub fn engine_execute(
    vault: Arc<Vault>,
    plan: Vec<SyncPlanEntry>,
    device_id: String,
    dav: Arc<dyn WebDav>,
    io: Arc<dyn SaveIo>,
    progress: Arc<dyn SyncProgressSink>,
) -> Result<SyncSummary, WaystoneError> {
    let core = plan
        .iter()
        .map(entry_to_core)
        .collect::<Result<Vec<_>, _>>()?;
    let summary = eng::execute(
        vault.core_vault(),
        &core,
        &device_id,
        &WebDavBridge { inner: dav },
        &IoBridge(io),
        &SinkBridge(progress),
    );
    Ok(summary_from_core(summary))
}

#[uniffi::export]
#[allow(clippy::too_many_arguments)]
pub fn engine_pull_hash(
    vault: Arc<Vault>,
    save: NormalizedSave,
    hash: String,
    head_mtime: String,
    device_id: String,
    dav: Arc<dyn WebDav>,
    io: Arc<dyn SaveIo>,
) -> Result<bool, WaystoneError> {
    let core = to_core_save(&save)?;
    Ok(eng::pull_hash(
        vault.core_vault(),
        0,
        &core,
        &hash,
        &head_mtime,
        &device_id,
        &WebDavBridge { inner: dav },
        &IoBridge(io),
    )?)
}

/// History restore: write `hash` through `io` WITHOUT touching this device's head.
#[uniffi::export]
pub fn engine_restore_hash(
    vault: Arc<Vault>,
    save: NormalizedSave,
    hash: String,
    dav: Arc<dyn WebDav>,
    io: Arc<dyn SaveIo>,
) -> Result<bool, WaystoneError> {
    let core = to_core_save(&save)?;
    Ok(eng::restore_hash(
        vault.core_vault(),
        0,
        &core,
        &hash,
        &WebDavBridge { inner: dav },
        &IoBridge(io),
    )?)
}

#[uniffi::export]
pub fn sync_summary_headline(summary: SyncSummary) -> String {
    summary_to_core(&summary).headline()
}

#[uniffi::export]
pub fn remote_only_saves(
    vault: Arc<Vault>,
    roms: Vec<RomRef>,
    local_group_keys: Vec<String>,
    dav: Arc<dyn WebDav>,
) -> Result<Vec<NormalizedSave>, WaystoneError> {
    let roms = roms
        .into_iter()
        .map(|r| {
            Ok(ro::RomRef {
                system: parse_system(&r.system)?,
                rom_id: r.rom_id,
                display_name: r.display_name,
            })
        })
        .collect::<Result<Vec<_>, WaystoneError>>()?;
    let local: BTreeSet<String> = local_group_keys.into_iter().collect();
    let found = ro::remote_only_saves(
        vault.core_vault(),
        &roms,
        &local,
        &WebDavBridge { inner: dav },
    );
    Ok(found.iter().map(from_core_save).collect())
}
