use crate::decision::{PullOutcome, PushOutcome, SyncDecision};
use crate::error::WaystoneError;
use crate::types::{FileEntry, NormalizedSave, RawTree};
use crate::vault::Vault;
use crate::webdav::{WebDav, WebDavBridge};
use std::sync::Arc;
use waystone_core::adapters::Adapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::model as core_model;

fn to_core_save(save: &NormalizedSave) -> core_model::NormalizedSave {
    core_model::NormalizedSave {
        id: core_model::SaveId {
            source: save.source.clone(),
            system: parse_system(&save.system),
            game: core_model::GameRef {
                key: save.game_key.clone(),
                display_name: save.display_name.clone(),
                confidence: core_model::Confidence::Strong,
                title_id: save.title_id.clone(),
                serial: None,
                rom_crc: None,
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
    }
}

fn from_core_save(save: &core_model::NormalizedSave) -> NormalizedSave {
    NormalizedSave {
        source: save.id.source.clone(),
        system: save.id.system.as_str().to_string(),
        game_key: save.id.game.key.clone(),
        display_name: save.id.game.display_name.clone(),
        title_id: save.id.game.title_id.clone(),
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

fn parse_system(s: &str) -> core_model::SystemId {
    match s {
        "switch" => core_model::SystemId::Switch,
        "3ds" => core_model::SystemId::ThreeDS,
        "nds" => core_model::SystemId::Nds,
        "gba" => core_model::SystemId::Gba,
        "gbc" => core_model::SystemId::Gbc,
        "gb" => core_model::SystemId::Gb,
        _ => core_model::SystemId::Switch,
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

#[uniffi::export]
pub fn jksv_normalize(raw: RawTree) -> Vec<NormalizedSave> {
    let core_raw = core_model::RawTree {
        files: raw
            .files
            .into_iter()
            .map(|f| core_model::RawFile {
                path: f.path,
                content: f.content,
            })
            .collect(),
    };
    let adapter = JksvAdapter::new(core_model::SystemId::Switch);
    adapter
        .normalize(&core_raw)
        .iter()
        .map(from_core_save)
        .collect()
}

#[uniffi::export]
pub fn push_one(
    vault: Arc<Vault>,
    save: NormalizedSave,
    device_id: String,
    dav: Arc<dyn WebDav>,
) -> Result<PushOutcome, WaystoneError> {
    let core_save = to_core_save(&save);
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
) -> Result<PullOutcome, WaystoneError> {
    let core_save = to_core_save(&save);
    let bridge = WebDavBridge { inner: dav };
    let outcome = waystone_sync::pull_one(
        vault.core_vault(),
        &core_save,
        &device_id,
        waystone_core::conflict::ConflictPolicy::NewestWins,
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
