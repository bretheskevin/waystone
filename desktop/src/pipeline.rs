use anyhow::{Context, Result};
use waystone_core::conflict::{fold_heads, three_way_sync, ConflictPolicy, DeviceHead, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_core::packaging;
use crate::webdav::WebDavClient;

pub struct SyncPipeline<'a> {
    pub vault: &'a Vault,
    pub dav: &'a WebDavClient,
    pub device_id: &'a str,
    #[allow(dead_code)]
    pub policy: ConflictPolicy,
}

impl<'a> SyncPipeline<'a> {
    fn remote_path(&self, save: &NormalizedSave) -> String {
        let sys_seg = self.vault.path_segment(save.id.system.as_str());
        let game_seg = self.vault.path_segment(&save.id.game.key);
        let slot_seg = self.vault.path_segment(&save.id.slot);
        format!("{}/{}/{}", sys_seg, game_seg, slot_seg)
    }

    pub async fn push(&self, save: &NormalizedSave) -> Result<()> {
        let (entry, zip_bytes) = packaging::package(save);
        let encrypted = self.vault.encrypt_blob(&zip_bytes)?;
        let blob_name = self.vault.blob_name(&entry.content.hash);
        let base_path = self.remote_path(save);

        self.dav.mkdir_p(&format!("{}/blobs", base_path)).await?;
        self.dav.mkdir_p(&format!("{}/heads", base_path)).await?;
        self.dav.mkdir_p(&format!("{}/history", base_path)).await?;

        let blob_path = format!("{}/blobs/{}.bin", base_path, blob_name);
        if !self.dav.exists(&blob_path).await? {
            self.dav.put(&blob_path, encrypted).await?;
        }

        let head = DeviceHead {
            device_id: self.device_id.to_string(),
            hash: entry.content.hash.clone(),
            mtime: entry.mtime.clone(),
        };
        let head_json = serde_json::to_vec_pretty(&head)?;
        let encrypted_head = self.vault.encrypt_heads(&head_json)?;
        let head_path = format!("{}/heads/{}.json", base_path, self.device_id);
        self.dav.put(&head_path, encrypted_head).await?;

        let ts = chrono::Utc::now().format("%Y%m%dT%H%M%SZ");
        let history_path = format!("{}/history/{}-{}.json", base_path, ts, self.device_id);
        let encrypted_history = self.vault.encrypt_heads(&head_json)?;
        self.dav.put(&history_path, encrypted_history).await?;

        Ok(())
    }

    pub async fn pull_blob(&self, save: &NormalizedSave, hash: &str) -> Result<Vec<u8>> {
        let blob_name = self.vault.blob_name(hash);
        let base_path = self.remote_path(save);
        let blob_path = format!("{}/blobs/{}.bin", base_path, blob_name);

        let encrypted = self.dav.get(&blob_path).await?
            .context("blob not found on server")?;
        let zip_bytes = self.vault.decrypt_blob(&encrypted)?;
        Ok(zip_bytes)
    }

    #[allow(dead_code)]
    pub async fn read_remote_head(&self, save: &NormalizedSave, device_id: &str) -> Result<Option<DeviceHead>> {
        let base_path = self.remote_path(save);
        let head_path = format!("{}/heads/{}.json", base_path, device_id);

        let Some(encrypted) = self.dav.get(&head_path).await? else {
            return Ok(None);
        };
        let json = self.vault.decrypt_heads(&encrypted)?;
        let head: DeviceHead = serde_json::from_slice(&json)?;
        Ok(Some(head))
    }

    /// PROPFIND the heads/ collection and return all device heads found there.
    pub async fn read_all_remote_heads(&self, save: &NormalizedSave) -> Result<Vec<DeviceHead>> {
        let base_path = self.remote_path(save);
        let heads_path = format!("{}/heads", base_path);
        let hrefs = self.dav.propfind(&heads_path).await?;

        let mut heads = Vec::new();
        for href in hrefs {
            let filename = href.trim_end_matches('/').rsplit('/').next().unwrap_or("");
            if filename.is_empty() || !filename.ends_with(".json") {
                continue;
            }
            let path = format!("{}/{}", heads_path, filename);
            let Some(encrypted) = self.dav.get(&path).await? else {
                continue;
            };
            let Ok(json) = self.vault.decrypt_heads(&encrypted) else {
                continue;
            };
            let Ok(head) = serde_json::from_slice::<DeviceHead>(&json) else {
                continue;
            };
            heads.push(head);
        }
        Ok(heads)
    }
}

/// Compute the sync decision for a pull given all remote heads for a save.
///
/// `base` is this device's own last-pushed head (if any); `merged` is the fold of all
/// remote heads.  Passing them into `three_way_sync` produces the correct Pull/Push/InSync
/// decision without the base==head bug that makes Pull impossible.
pub fn decide_pull(
    local_hash: Option<&str>,
    local_mtime: &str,
    all_remote_heads: &[DeviceHead],
    this_device_id: &str,
    policy: ConflictPolicy,
) -> SyncDecision {
    let Some(merged) = fold_heads(all_remote_heads) else {
        return if local_hash.is_some() { SyncDecision::Push } else { SyncDecision::InSync };
    };
    let base = all_remote_heads
        .iter()
        .find(|h| h.device_id == this_device_id)
        .map(|h| h.hash.as_str());
    three_way_sync(
        local_hash,
        base,
        Some(merged.hash.as_str()),
        local_mtime,
        &merged.mtime,
        policy,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decide_pull_yields_pull_when_remote_newer() {
        let all_heads = vec![
            DeviceHead {
                device_id: "dev1".into(),
                hash: "hash_a".into(),
                mtime: "2026-01-01T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "dev2".into(),
                hash: "hash_b".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
            },
        ];
        let decision = decide_pull(
            Some("hash_a"),
            "2026-01-01T00:00:00Z",
            &all_heads,
            "dev1",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(decision, SyncDecision::Pull { head_hash: "hash_b".into() });
    }

    #[test]
    fn decide_pull_in_sync_when_already_have_newest() {
        let all_heads = vec![
            DeviceHead {
                device_id: "dev1".into(),
                hash: "hash_b".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "dev2".into(),
                hash: "hash_b".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
            },
        ];
        let decision = decide_pull(
            Some("hash_b"),
            "2026-01-02T00:00:00Z",
            &all_heads,
            "dev1",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(decision, SyncDecision::InSync);
    }

    #[test]
    fn decide_pull_no_remote_heads_yields_push_when_local_exists() {
        let decision = decide_pull(
            Some("hash_a"),
            "2026-01-01T00:00:00Z",
            &[],
            "dev1",
            ConflictPolicy::NewestWins,
        );
        assert_eq!(decision, SyncDecision::Push);
    }

    #[test]
    fn decide_pull_new_device_no_local_yields_pull() {
        let all_heads = vec![DeviceHead {
            device_id: "dev1".into(),
            hash: "hash_a".into(),
            mtime: "2026-01-01T00:00:00Z".into(),
        }];
        // dev2 is brand-new: no local save, not in heads yet
        let decision = decide_pull(None, "", &all_heads, "dev2", ConflictPolicy::NewestWins);
        assert_eq!(decision, SyncDecision::Pull { head_hash: "hash_a".into() });
    }

    /// Live two-device round-trip against a real dufs instance.
    ///
    /// Simulates the common case: both devices previously synced the same save,
    /// device-1 updated it and pushed, device-2 now pulls and gets the newer content.
    ///
    /// Requires: docker run -d -v <vol>:/data -p 5099:5000 sigoden/dufs /data --allow-all
    #[tokio::test]
    #[ignore]
    async fn two_device_push_pull_round_trip() {
        use crate::webdav::WebDavClient;
        use waystone_core::conflict::{ConflictPolicy, ConflictWinner, SyncDecision};
        use waystone_core::crypto::Vault;
        use waystone_core::model::{
            build_group_key, Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId,
        };

        let server = "http://localhost:5099";

        let (vault1, _recovery) = Vault::init("integration-test").unwrap();
        let keys_json = vault1.keys_json().unwrap();

        let dav1 = WebDavClient::new(server, None, None);
        dav1.put("/keys.json", keys_json.clone()).await.unwrap();

        let game_id = SaveId {
            source: "jksv".into(),
            system: SystemId::Switch,
            game: GameRef {
                key: "INTEGRATION_GAME_001".into(),
                display_name: "Integration Test Game".into(),
                confidence: Confidence::Strong,
                title_id: Some("INTEGRATION_GAME_001".into()),
                serial: None,
                rom_crc: None,
            },
            slot: "main".into(),
            kind: SaveKind::Native,
        };
        let group_key = build_group_key(SystemId::Switch, "INTEGRATION_GAME_001", "main");

        let vault2 = Vault::unlock_with_passphrase("integration-test", &keys_json).unwrap();
        let dav2 = WebDavClient::new(server, None, None);

        let pipe1 = SyncPipeline {
            vault: &vault1,
            dav: &dav1,
            device_id: "integration-device-1",
            policy: ConflictPolicy::NewestWins,
        };
        let pipe2 = SyncPipeline {
            vault: &vault2,
            dav: &dav2,
            device_id: "integration-device-2",
            policy: ConflictPolicy::NewestWins,
        };

        // Both devices start with the same old save and push it (establishing heads).
        let save_old = NormalizedSave {
            id: game_id.clone(),
            group_key: group_key.clone(),
            portable: true,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![("save.dat".into(), b"shared-initial-save".to_vec())],
        };
        pipe1.push(&save_old).await.unwrap();
        pipe2.push(&save_old).await.unwrap();

        // Device 1 makes progress and pushes the newer save.
        let save_new = NormalizedSave {
            id: game_id.clone(),
            group_key: group_key.clone(),
            portable: true,
            mtime: "2026-01-02T00:00:00Z".into(),
            files: vec![("save.dat".into(), b"device1-progress-content".to_vec())],
        };
        pipe1.push(&save_new).await.unwrap();

        // Device 2 still has the old save locally (same content as what it pushed).
        let all_heads = pipe2.read_all_remote_heads(&save_old).await.unwrap();
        assert_eq!(all_heads.len(), 2, "should find both device heads");

        let (entry2_old, _) = waystone_core::packaging::package(&save_old);
        let decision = decide_pull(
            Some(&entry2_old.content.hash),
            &entry2_old.mtime,
            &all_heads,
            "integration-device-2",
            ConflictPolicy::NewestWins,
        );

        // local == base (device-2 hasn't changed since last push) → clean Pull
        let head_hash = match &decision {
            SyncDecision::Pull { head_hash } => head_hash.clone(),
            SyncDecision::ConflictResolved { winner: ConflictWinner::Remote, .. } => {
                fold_heads(&all_heads).unwrap().hash
            }
            other => panic!("expected Pull or ConflictResolved(Remote), got {:?}", other),
        };

        let zip_bytes = pipe2.pull_blob(&save_old, &head_hash).await.unwrap();
        let files = waystone_core::packaging::unzip(&zip_bytes).unwrap();
        let save_file = files.iter().find(|(p, _)| p == "save.dat").expect("save.dat");
        assert_eq!(
            save_file.1,
            b"device1-progress-content",
            "device-2 should have received device-1's updated save"
        );
    }
}
