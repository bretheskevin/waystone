use crate::decision::{ConflictPolicy, PullOutcome, PushOutcome, SyncDecision};
use crate::error::WaystoneError;
use crate::types::{Confidence, FileEntry, NormalizedSave, RawFileEntry, RawTree};
use crate::vault::Vault;
use crate::webdav::{WebDav, WebDavBridge};
use std::sync::Arc;
use waystone_core::adapters::Adapter;
use waystone_core::adapters::checkpoint::CheckpointAdapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::adapters::twilight::TwilightAdapter;
use waystone_core::model as core_model;

fn to_core_save(save: &NormalizedSave) -> Result<core_model::NormalizedSave, WaystoneError> {
    Ok(core_model::NormalizedSave {
        id: core_model::SaveId {
            source: save.source.clone(),
            system: parse_system(&save.system)?,
            game: core_model::GameRef {
                key: save.game_key.clone(),
                display_name: save.display_name.clone(),
                confidence: match save.confidence {
                    Confidence::Strong => core_model::Confidence::Strong,
                    Confidence::Weak => core_model::Confidence::Weak,
                },
                title_id: save.title_id.clone(),
                serial: save.serial.clone(),
                rom_crc: save.rom_crc.clone(),
            },
            slot: save.slot.clone(),
            kind: parse_kind(&save.kind),
        },
        group_key: save.group_key.clone(),
        portable: save.portable,
        mtime: save.mtime.clone(),
        files: save
            .files
            .iter()
            .map(|f| (f.path.clone(), f.content.clone()))
            .collect(),
    })
}

fn from_core_save(save: &core_model::NormalizedSave) -> NormalizedSave {
    NormalizedSave {
        source: save.id.source.clone(),
        system: save.id.system.as_str().to_string(),
        game_key: save.id.game.key.clone(),
        display_name: save.id.game.display_name.clone(),
        title_id: save.id.game.title_id.clone(),
        serial: save.id.game.serial.clone(),
        rom_crc: save.id.game.rom_crc.clone(),
        confidence: match save.id.game.confidence {
            core_model::Confidence::Strong => Confidence::Strong,
            core_model::Confidence::Weak => Confidence::Weak,
        },
        slot: save.id.slot.clone(),
        kind: match save.id.kind {
            core_model::SaveKind::Battery => "battery".into(),
            core_model::SaveKind::SaveState => "savestate".into(),
            core_model::SaveKind::Native => "native".into(),
        },
        group_key: save.group_key.clone(),
        portable: save.portable,
        mtime: save.mtime.clone(),
        files: save
            .files
            .iter()
            .map(|f| FileEntry {
                path: f.0.clone(),
                content: f.1.clone(),
            })
            .collect(),
    }
}

fn parse_system(s: &str) -> Result<core_model::SystemId, WaystoneError> {
    match s {
        "switch" => Ok(core_model::SystemId::Switch),
        "3ds" => Ok(core_model::SystemId::ThreeDS),
        "nds" => Ok(core_model::SystemId::Nds),
        "gba" => Ok(core_model::SystemId::Gba),
        "gbc" => Ok(core_model::SystemId::Gbc),
        "gb" => Ok(core_model::SystemId::Gb),
        other => Err(WaystoneError::InvalidSystem {
            system: other.to_string(),
        }),
    }
}

fn parse_kind(s: &str) -> core_model::SaveKind {
    match s {
        "battery" => core_model::SaveKind::Battery,
        "savestate" => core_model::SaveKind::SaveState,
        _ => core_model::SaveKind::Native,
    }
}

fn from_core_decision(d: &waystone_core::conflict::SyncDecision) -> SyncDecision {
    match d {
        waystone_core::conflict::SyncDecision::InSync => SyncDecision::InSync,
        waystone_core::conflict::SyncDecision::Push => SyncDecision::Push,
        waystone_core::conflict::SyncDecision::Pull { head_hash } => SyncDecision::Pull {
            head_hash: head_hash.clone(),
        },
        waystone_core::conflict::SyncDecision::ConflictResolved { winner, loser_hash } => {
            let w = match winner {
                waystone_core::conflict::ConflictWinner::Local => "local",
                waystone_core::conflict::ConflictWinner::Remote => "remote",
            };
            SyncDecision::ConflictResolved {
                winner: w.to_string(),
                loser_hash: loser_hash.clone(),
            }
        }
        waystone_core::conflict::SyncDecision::ConflictNeedsInput {
            local_hash,
            remote_hash,
        } => SyncDecision::ConflictNeedsInput {
            local_hash: local_hash.clone(),
            remote_hash: remote_hash.clone(),
        },
    }
}

fn map_conflict_policy(policy: ConflictPolicy) -> waystone_core::conflict::ConflictPolicy {
    match policy {
        ConflictPolicy::NewestWins => waystone_core::conflict::ConflictPolicy::NewestWins,
        ConflictPolicy::Prompt => waystone_core::conflict::ConflictPolicy::Prompt,
    }
}

fn to_core_raw(raw: RawTree) -> core_model::RawTree {
    core_model::RawTree {
        files: raw
            .files
            .into_iter()
            .map(|f| core_model::RawFile {
                path: f.path,
                content: f.content,
            })
            .collect(),
    }
}

fn from_core_raw(raw: core_model::RawTree) -> RawTree {
    RawTree {
        files: raw
            .files
            .into_iter()
            .map(|f| RawFileEntry {
                path: f.path,
                content: f.content,
            })
            .collect(),
    }
}

fn normalize_via<A: Adapter>(
    system: &str,
    raw: RawTree,
    build: impl FnOnce(core_model::SystemId) -> A,
) -> Result<Vec<NormalizedSave>, WaystoneError> {
    let sys = parse_system(system)?;
    let adapter = build(sys);
    let core_raw = to_core_raw(raw);
    Ok(adapter
        .normalize(&core_raw)
        .iter()
        .map(from_core_save)
        .collect())
}

fn to_native_via(
    save: NormalizedSave,
    run: impl FnOnce(&core_model::NormalizedSave) -> core_model::RawTree,
) -> Result<RawTree, WaystoneError> {
    let core_save = to_core_save(&save)?;
    Ok(from_core_raw(run(&core_save)))
}

#[uniffi::export]
pub fn jksv_normalize(system: String, raw: RawTree) -> Result<Vec<NormalizedSave>, WaystoneError> {
    normalize_via(&system, raw, JksvAdapter::new)
}

#[uniffi::export]
pub fn jksv_to_native(save: NormalizedSave) -> Result<RawTree, WaystoneError> {
    to_native_via(save, |s| JksvAdapter::new(s.id.system).to_native(s))
}

#[uniffi::export]
pub fn mgba_normalize(system: String, raw: RawTree) -> Result<Vec<NormalizedSave>, WaystoneError> {
    normalize_via(&system, raw, MgbaAdapter::new)
}

#[uniffi::export]
pub fn mgba_to_native(save: NormalizedSave) -> Result<RawTree, WaystoneError> {
    to_native_via(save, |s| MgbaAdapter::new(s.id.system).to_native(s))
}

#[uniffi::export]
pub fn twilight_normalize(raw: RawTree) -> Vec<NormalizedSave> {
    let core_raw = to_core_raw(raw);
    TwilightAdapter::new()
        .normalize(&core_raw)
        .iter()
        .map(from_core_save)
        .collect()
}

#[uniffi::export]
pub fn twilight_to_native(save: NormalizedSave) -> Result<RawTree, WaystoneError> {
    to_native_via(save, |s| TwilightAdapter::new().to_native(s))
}

#[uniffi::export]
pub fn checkpoint_normalize(
    system: String,
    raw: RawTree,
) -> Result<Vec<NormalizedSave>, WaystoneError> {
    normalize_via(&system, raw, CheckpointAdapter::new)
}

#[uniffi::export]
pub fn checkpoint_to_native(save: NormalizedSave) -> Result<RawTree, WaystoneError> {
    to_native_via(save, |s| CheckpointAdapter::new(s.id.system).to_native(s))
}

#[uniffi::export]
pub fn push_one(
    vault: Arc<Vault>,
    save: NormalizedSave,
    device_id: String,
    dav: Arc<dyn WebDav>,
) -> Result<PushOutcome, WaystoneError> {
    let core_save = to_core_save(&save)?;
    let bridge = WebDavBridge { inner: dav };
    let outcome = waystone_sync::push_one(vault.core_vault(), &core_save, &device_id, &bridge)?;
    Ok(match outcome {
        waystone_sync::PushOutcome::Pushed => PushOutcome::Pushed,
        waystone_sync::PushOutcome::BlobExisted => PushOutcome::BlobExisted,
    })
}

#[uniffi::export]
pub fn pull_one(
    vault: Arc<Vault>,
    save: NormalizedSave,
    device_id: String,
    dav: Arc<dyn WebDav>,
    policy: ConflictPolicy,
) -> Result<PullOutcome, WaystoneError> {
    let core_save = to_core_save(&save)?;
    let bridge = WebDavBridge { inner: dav };
    let outcome = waystone_sync::pull_one(
        vault.core_vault(),
        &core_save,
        &device_id,
        map_conflict_policy(policy),
        &bridge,
    )?;
    Ok(PullOutcome {
        decision: from_core_decision(&outcome.decision),
        files: outcome.files.map(|fs| {
            fs.into_iter()
                .map(|(p, c)| FileEntry {
                    path: p,
                    content: c,
                })
                .collect()
        }),
    })
}
