use crate::decision::{ConflictPolicy, PullOutcome, PushOutcome, SyncDecision};
use crate::error::WaystoneError;
use crate::types::{
    Confidence, DeviceHead, FileEntry, HistoryEntry, NormalizedSave, RawFileEntry, RawTree,
    RomPairing,
};
use crate::vault::Vault;
use crate::webdav::{WebDav, WebDavBridge};
use std::sync::Arc;
use waystone_core::adapters::Adapter;
use waystone_core::adapters::checkpoint::CheckpointAdapter;
use waystone_core::adapters::jksv::JksvAdapter;
use waystone_core::adapters::mgba::MgbaAdapter;
use waystone_core::adapters::rom_keyed::{
    RomKeyedAdapter, rom_keyed_to_native as core_rom_keyed_to_native,
};
use waystone_core::crc32::crc32_update as core_crc32_update;
use waystone_core::model as core_model;
use waystone_core::rom_id::{
    display_name as core_display_name, needs_full_hash, rom_identity as core_rom_identity,
};
use waystone_core::rom_pair::pair_roms;
use waystone_core::rom_systems::rom_system;

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
    core_model::SystemId::parse(s).ok_or_else(|| WaystoneError::InvalidSystem {
        system: s.to_string(),
    })
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

#[uniffi::export]
pub fn read_remote_heads(
    vault: Arc<Vault>,
    save: NormalizedSave,
    dav: Arc<dyn WebDav>,
) -> Result<Vec<DeviceHead>, WaystoneError> {
    let core_save = to_core_save(&save)?;
    let heads = waystone_sync::read_remote_heads(
        vault.core_vault(),
        &core_save,
        &WebDavBridge { inner: dav },
    )?;
    Ok(heads.into_iter().map(DeviceHead::from).collect())
}

#[uniffi::export]
pub fn list_history(
    vault: Arc<Vault>,
    save: NormalizedSave,
    dav: Arc<dyn WebDav>,
) -> Result<Vec<HistoryEntry>, WaystoneError> {
    let core_save = to_core_save(&save)?;
    let entries =
        waystone_sync::list_history(vault.core_vault(), &core_save, &WebDavBridge { inner: dav })?;
    Ok(entries.into_iter().map(HistoryEntry::from).collect())
}

#[uniffi::export]
pub fn fetch_blob(
    vault: Arc<Vault>,
    save: NormalizedSave,
    hash: String,
    dav: Arc<dyn WebDav>,
) -> Result<Vec<u8>, WaystoneError> {
    let core_save = to_core_save(&save)?;
    Ok(waystone_sync::fetch_blob(
        vault.core_vault(),
        &core_save,
        &hash,
        &WebDavBridge { inner: dav },
    )?)
}

#[uniffi::export]
pub fn decide_pull(
    local_hash: Option<String>,
    local_mtime: String,
    heads: Vec<DeviceHead>,
    device_id: String,
    policy: ConflictPolicy,
) -> Result<SyncDecision, WaystoneError> {
    let core_heads: Vec<waystone_core::conflict::DeviceHead> = heads
        .into_iter()
        .map(|h| waystone_core::conflict::DeviceHead {
            device_id: h.device_id,
            hash: h.hash,
            mtime: h.mtime,
        })
        .collect();
    Ok(from_core_decision(&waystone_core::conflict::decide_pull(
        local_hash.as_deref(),
        &local_mtime,
        &core_heads,
        &device_id,
        map_conflict_policy(policy),
    )))
}

// MergedHead has no device_id; the empty string is intentional.
#[uniffi::export]
pub fn fold_heads(heads: Vec<DeviceHead>) -> Option<DeviceHead> {
    let core_heads: Vec<waystone_core::conflict::DeviceHead> = heads
        .into_iter()
        .map(|h| waystone_core::conflict::DeviceHead {
            device_id: h.device_id,
            hash: h.hash,
            mtime: h.mtime,
        })
        .collect();
    waystone_core::conflict::fold_heads(&core_heads).map(|m| DeviceHead {
        device_id: String::new(),
        hash: m.hash,
        mtime: m.mtime,
    })
}

#[uniffi::export]
pub fn local_hash(save: NormalizedSave) -> Result<String, WaystoneError> {
    let core_save = to_core_save(&save)?;
    let (_, zip) = waystone_core::packaging::package(&core_save);
    Ok(waystone_core::packaging::content_hash(&zip))
}

fn parse_rom_system(s: &str) -> Result<core_model::SystemId, WaystoneError> {
    let sys = parse_system(s)?;
    if rom_system(sys).is_none() {
        return Err(WaystoneError::InvalidSystem {
            system: s.to_string(),
        });
    }
    Ok(sys)
}

#[uniffi::export]
pub fn crc32_update(crc: u32, data: Vec<u8>) -> u32 {
    core_crc32_update(crc, &data)
}

#[uniffi::export]
pub fn rom_needs_full_hash(system: String, header: Vec<u8>) -> Result<bool, WaystoneError> {
    Ok(needs_full_hash(parse_rom_system(&system)?, &header))
}

#[uniffi::export]
pub fn rom_identity(
    system: String,
    header: Vec<u8>,
    full_crc32: Option<u32>,
) -> Result<Option<String>, WaystoneError> {
    Ok(core_rom_identity(
        parse_rom_system(&system)?,
        &header,
        full_crc32,
    ))
}

#[uniffi::export]
pub fn rom_display_name(
    system: String,
    header: Vec<u8>,
    rom_file_name: String,
) -> Result<String, WaystoneError> {
    Ok(core_display_name(
        parse_rom_system(&system)?,
        &header,
        &rom_file_name,
    ))
}

#[uniffi::export]
pub fn rom_pair(paths: Vec<String>) -> Vec<RomPairing> {
    pair_roms(&paths)
        .into_iter()
        .map(|p| RomPairing {
            system: p.system.as_str().to_string(),
            rom_path: p.rom_path,
            save_dir: p.save_dir,
            save_paths: p.save_paths,
        })
        .collect()
}

#[uniffi::export]
pub fn rom_keyed_normalize(
    system: String,
    rom_id: String,
    display_name: String,
    rom_file_name: String,
    raw: RawTree,
) -> Result<Vec<NormalizedSave>, WaystoneError> {
    let adapter = RomKeyedAdapter::new(
        parse_rom_system(&system)?,
        rom_id,
        display_name,
        rom_file_name,
    );
    Ok(adapter
        .normalize(&to_core_raw(raw))
        .iter()
        .map(from_core_save)
        .collect())
}

#[uniffi::export]
pub fn rom_keyed_to_native(
    save: NormalizedSave,
    rom_file_name: String,
) -> Result<RawTree, WaystoneError> {
    to_native_via(save, |s| core_model::RawTree {
        files: core_rom_keyed_to_native(s.id.system, &s.id.slot, &rom_file_name, &s.files)
            .into_iter()
            .map(|(path, content)| core_model::RawFile { path, content })
            .collect(),
    })
}
