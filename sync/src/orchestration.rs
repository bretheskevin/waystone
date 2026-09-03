use crate::{Result, SyncError, WebDav};
use waystone_core::conflict::{ConflictPolicy, ConflictWinner, DeviceHead, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_core::packaging;

#[derive(Debug)]
pub enum PushOutcome {
    Pushed,
    BlobExisted,
}

#[derive(Debug)]
pub struct PullOutcome {
    pub decision: SyncDecision,
    pub files: Option<Vec<(String, Vec<u8>)>>,
}

pub fn remote_path(vault: &Vault, save: &NormalizedSave) -> String {
    let sys_seg = vault.path_segment(save.id.system.as_str());
    let game_seg = vault.path_segment(&save.id.game.key);
    let slot_seg = vault.path_segment(&save.id.slot);
    format!("{}/{}/{}", sys_seg, game_seg, slot_seg)
}

pub fn push_one(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    dav: &dyn WebDav,
) -> Result<PushOutcome> {
    let (entry, zip_bytes) = packaging::package(save);
    let encrypted = vault.encrypt_blob(&zip_bytes)?;
    let blob_name = vault.blob_name(&entry.content.hash);
    let base_path = remote_path(vault, save);

    dav.mkdir_p(&format!("{}/blobs", base_path))?;
    dav.mkdir_p(&format!("{}/heads", base_path))?;
    dav.mkdir_p(&format!("{}/history", base_path))?;

    let blob_path = format!("{}/blobs/{}.bin", base_path, blob_name);
    let blob_existed = dav.exists(&blob_path)?;
    if !blob_existed {
        dav.put(&blob_path, encrypted)?;
    }

    let head = DeviceHead {
        device_id: device_id.to_string(),
        hash: entry.content.hash.clone(),
        mtime: entry.mtime.clone(),
    };
    let head_json = serde_json::to_vec_pretty(&head)?;
    let encrypted_head = vault.encrypt_heads(&head_json)?;
    let head_path = format!("{}/heads/{}.json", base_path, device_id);
    dav.put(&head_path, encrypted_head)?;

    let ts = chrono::Utc::now().format("%Y%m%dT%H%M%SZ");
    let history_path = format!("{}/history/{}-{}.json", base_path, ts, device_id);
    let encrypted_history = vault.encrypt_heads(&head_json)?;
    dav.put(&history_path, encrypted_history)?;

    if blob_existed {
        Ok(PushOutcome::BlobExisted)
    } else {
        Ok(PushOutcome::Pushed)
    }
}

pub fn read_remote_heads(
    vault: &Vault,
    save: &NormalizedSave,
    dav: &dyn WebDav,
) -> Result<Vec<DeviceHead>> {
    let base_path = remote_path(vault, save);
    let heads_path = format!("{}/heads", base_path);
    let hrefs = dav.propfind(&heads_path)?;

    let mut heads = Vec::new();
    for href in hrefs {
        let filename = href.trim_end_matches('/').rsplit('/').next().unwrap_or("");
        if filename.is_empty() || !filename.ends_with(".json") {
            continue;
        }
        // propfind may return full paths (real WebDAV) or just filenames (mocks)
        let path = if href.contains('/') {
            href.clone()
        } else {
            format!("{}/{}", heads_path, filename)
        };
        let Some(encrypted) = dav.get(&path)? else {
            continue;
        };
        let Ok(json) = vault.decrypt_heads(&encrypted) else {
            continue;
        };
        let Ok(head) = serde_json::from_slice::<DeviceHead>(&json) else {
            continue;
        };
        heads.push(head);
    }
    Ok(heads)
}

pub fn pull_one(
    vault: &Vault,
    save: &NormalizedSave,
    device_id: &str,
    policy: ConflictPolicy,
    dav: &dyn WebDav,
) -> Result<PullOutcome> {
    let all_heads = read_remote_heads(vault, save, dav)?;
    let (entry, _) = packaging::package(save);

    let decision = waystone_core::conflict::decide_pull(
        Some(&entry.content.hash),
        &entry.mtime,
        &all_heads,
        device_id,
        policy,
    );

    let pull_hash: Option<String> = match &decision {
        SyncDecision::Pull { head_hash } => Some(head_hash.clone()),
        SyncDecision::ConflictResolved {
            winner: ConflictWinner::Remote,
            ..
        } => waystone_core::conflict::fold_heads(&all_heads).map(|m| m.hash),
        _ => None,
    };

    let files = match pull_hash {
        Some(hash) => {
            let blob_name = vault.blob_name(&hash);
            let base_path = remote_path(vault, save);
            let blob_path = format!("{}/blobs/{}.bin", base_path, blob_name);
            let encrypted = dav
                .get(&blob_path)?
                .ok_or(SyncError::BlobNotFound(blob_path))?;
            let zip_bytes = vault.decrypt_blob(&encrypted)?;
            Some(packaging::unzip(&zip_bytes)?)
        }
        None => None,
    };

    Ok(PullOutcome { decision, files })
}
