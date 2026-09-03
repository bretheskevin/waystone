use crate::config::{SyncTarget, WaystoneConfig};
use crate::helpers;
use crate::pipeline::SyncPipeline;
use crate::tui::app::{ActionResult, ConflictEntry, HeadInfo, Msg, SessionCreds, TargetStatus};
use crate::webdav::WebDavClient;
use anyhow::Result;
use std::sync::Arc;
use tokio::sync::mpsc;
use waystone_core::conflict::{self, ConflictWinner, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use zeroize::Zeroize;

/// Builds the normalized save list for `target` synchronously.
///
/// `Box<dyn Adapter>` is `!Send`. The adapter is created and dropped inside
/// this function so the returned `Vec<NormalizedSave>` is `Send` and callers
/// can hold it across `.await` points without triggering a `!Send` error.
pub(crate) fn load_target_saves(target: &SyncTarget) -> anyhow::Result<Vec<NormalizedSave>> {
    let system = helpers::parse_system(&target.system)?;
    let adapter = helpers::make_adapter(&target.adapter, system)?;
    let raw = helpers::read_source_tree(&target.path)?;
    Ok(adapter.normalize(&raw))
}

pub async fn push_target(
    target: &SyncTarget,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: "reading local tree".into(),
        })
        .await;

    // Collect saves synchronously before any await (Adapter is not Send)
    let saves: Vec<NormalizedSave> = load_target_saves(target)?;

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("pushing {} save(s)", saves.len()),
        })
        .await;

    let pipe = SyncPipeline {
        vault: &creds.vault,
        dav: &creds.dav,
        device_id: &config.device_id,
        policy: config.conflict_policy,
    };

    let count = saves.len();
    for (i, save) in saves.iter().enumerate() {
        let _ = tx
            .send(Msg::Progress {
                target_id,
                phase: format!(
                    "pushing {}/{}: {} / {}",
                    i + 1,
                    count,
                    save.id.game.display_name,
                    save.id.slot
                ),
            })
            .await;
        pipe.push(save).await?;
    }

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("{} save(s) pushed", count)),
        })
        .await;
    Ok(())
}

pub async fn pull_target(
    target: &SyncTarget,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: "reading local tree".into(),
        })
        .await;

    let dest = target.path.clone();

    // Collect saves synchronously before any await (Adapter is not Send)
    let saves: Vec<NormalizedSave> = load_target_saves(target)?;

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("checking {} save(s)", saves.len()),
        })
        .await;

    let pipe = SyncPipeline {
        vault: &creds.vault,
        dav: &creds.dav,
        device_id: &config.device_id,
        policy: config.conflict_policy,
    };

    let count = saves.len();
    let mut pulled = 0usize;
    for (i, save) in saves.iter().enumerate() {
        let _ = tx
            .send(Msg::Progress {
                target_id,
                phase: format!(
                    "checking {}/{}: {} / {}",
                    i + 1,
                    count,
                    save.id.game.display_name,
                    save.id.slot
                ),
            })
            .await;

        let all_heads = pipe.read_all_remote_heads(save).await?;
        let (entry, _) = waystone_core::packaging::package(save);
        let decision = conflict::decide_pull(
            Some(&entry.content.hash),
            &entry.mtime,
            &all_heads,
            &config.device_id,
            config.conflict_policy,
        );

        let pull_hash: Option<String> = match &decision {
            SyncDecision::Pull { head_hash } => Some(head_hash.clone()),
            SyncDecision::ConflictResolved {
                winner: ConflictWinner::Remote,
                ..
            } => conflict::fold_heads(&all_heads).map(|m| m.hash),
            _ => None,
        };

        if let Some(hash) = pull_hash {
            let _ = tx
                .send(Msg::Progress {
                    target_id,
                    phase: format!("pulling {} / {}", save.id.game.display_name, save.id.slot),
                })
                .await;
            let zip_bytes = pipe.pull_blob(save, &hash).await?;
            helpers::restore_save_from_blob(
                &zip_bytes,
                save,
                &dest,
                &target.adapter,
                &target.system,
            )?;
            pulled += 1;
        }
    }

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("{} save(s) pulled", pulled)),
        })
        .await;
    Ok(())
}

pub async fn refresh_status(
    target: &SyncTarget,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Status {
            target_id,
            status: TargetStatus::Checking,
        })
        .await;

    // Collect saves synchronously (Adapter is not Send)
    let saves: Vec<NormalizedSave> = load_target_saves(target)?;

    let pipe = SyncPipeline {
        vault: &creds.vault,
        dav: &creds.dav,
        device_id: &config.device_id,
        policy: config.conflict_policy,
    };

    let mut worst = TargetStatus::InSync;
    let mut conflict_entries: Vec<ConflictEntry> = Vec::new();

    for save in &saves {
        let all_heads = pipe.read_all_remote_heads(save).await?;
        let (entry, _) = waystone_core::packaging::package(save);

        // Always use Prompt for detection — surfaces all true divergences regardless of config.
        let decision = conflict::decide_pull(
            Some(&entry.content.hash),
            &entry.mtime,
            &all_heads,
            &config.device_id,
            conflict::ConflictPolicy::Prompt,
        );

        let status = match &decision {
            SyncDecision::InSync => TargetStatus::InSync,
            SyncDecision::Pull { .. } => TargetStatus::Behind,
            SyncDecision::Push => TargetStatus::Ahead,
            SyncDecision::ConflictResolved { .. } => TargetStatus::Conflict,
            SyncDecision::ConflictNeedsInput { .. } => TargetStatus::Conflict,
        };
        worst = worst_status(worst, status);

        if let Some(ce) = build_conflict_entry(
            save,
            &decision,
            &entry.content.hash,
            &entry.mtime,
            &all_heads,
            &config.device_id,
            target_id,
            &target.name,
        ) {
            conflict_entries.push(ce);
        }
    }

    let _ = tx
        .send(Msg::Status {
            target_id,
            status: worst,
        })
        .await;

    let _ = tx
        .send(Msg::Conflicts {
            target_id,
            entries: conflict_entries,
        })
        .await;

    Ok(())
}

#[allow(clippy::too_many_arguments)]
pub fn build_conflict_entry(
    save: &NormalizedSave,
    decision: &SyncDecision,
    local_hash: &str,
    local_mtime: &str,
    all_heads: &[conflict::DeviceHead],
    device_id: &str,
    target_id: usize,
    target_name: &str,
) -> Option<ConflictEntry> {
    match decision {
        SyncDecision::ConflictNeedsInput { remote_hash, .. } => {
            let winning_head = all_heads
                .iter()
                .filter(|h| h.device_id != device_id)
                .max_by(|a, b| a.mtime.cmp(&b.mtime))?;

            Some(ConflictEntry {
                target_id,
                label: format!(
                    "{} \u{00b7} {}/{}",
                    target_name, save.id.game.display_name, save.id.slot
                ),
                save_key: save.group_key.clone(),
                local: HeadInfo {
                    hash: local_hash.to_owned(),
                    mtime: local_mtime.to_owned(),
                    device_id: None,
                },
                remote: HeadInfo {
                    hash: winning_head.hash.clone(),
                    mtime: winning_head.mtime.clone(),
                    device_id: Some(winning_head.device_id.clone()),
                },
                remote_hash: remote_hash.clone(),
            })
        }
        _ => None,
    }
}

pub async fn resolve_keep_local(
    target: &SyncTarget,
    save_key: &str,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("resolving (keep local): {}", save_key),
        })
        .await;

    // push_target pushes all saves for the target and sends ActionDone itself
    push_target(target, creds, config, tx, target_id).await
}

pub async fn resolve_keep_remote(
    target: &SyncTarget,
    save_key: &str,
    remote_hash: &str,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("resolving (keep remote): {}", save_key),
        })
        .await;

    let saves: Vec<NormalizedSave> = load_target_saves(target)?;
    let save = saves
        .iter()
        .find(|s| s.group_key == save_key)
        .ok_or_else(|| anyhow::anyhow!("save '{}' not found in target", save_key))?;

    let pipe = SyncPipeline {
        vault: &creds.vault,
        dav: &creds.dav,
        device_id: &config.device_id,
        policy: config.conflict_policy,
    };

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!(
                "pulling blob {}...",
                &remote_hash[..12.min(remote_hash.len())]
            ),
        })
        .await;

    let zip_bytes = pipe.pull_blob(save, remote_hash).await?;
    helpers::restore_save_from_blob(
        &zip_bytes,
        save,
        &target.path,
        &target.adapter,
        &target.system,
    )?;

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("kept remote for {}", save_key)),
        })
        .await;

    Ok(())
}

fn worst_status(a: TargetStatus, b: TargetStatus) -> TargetStatus {
    fn rank(s: &TargetStatus) -> u8 {
        match s {
            TargetStatus::InSync => 0,
            TargetStatus::Ahead => 1,
            TargetStatus::Behind => 2,
            TargetStatus::Conflict => 3,
            TargetStatus::Error(_) => 4,
            _ => 0,
        }
    }
    if rank(&b) > rank(&a) { b } else { a }
}

pub async fn attempt_unlock(
    server_url: String,
    username: Option<String>,
    mut passphrase: String,
    webdav_password: Option<String>,
    tx: mpsc::Sender<Msg>,
) {
    let result = try_unlock(&server_url, username, &passphrase, webdav_password).await;
    passphrase.zeroize();
    match result {
        Ok((vault, dav)) => {
            let _ = tx
                .send(Msg::UnlockOk {
                    vault: Arc::new(vault),
                    dav: Arc::new(dav),
                })
                .await;
        }
        Err(e) => {
            let _ = tx.send(Msg::UnlockFailed(e.to_string())).await;
        }
    }
    // webdav_password moved into WebDavClient (zeroized on its drop); passphrase zeroized above
}

async fn try_unlock(
    server_url: &str,
    username: Option<String>,
    passphrase: &str,
    webdav_password: Option<String>,
) -> Result<(Vault, WebDavClient)> {
    let dav = WebDavClient::new(server_url, username, webdav_password);
    let keys_data = dav
        .get("/keys.json")
        .await?
        .ok_or_else(|| anyhow::anyhow!("no keys.json on server -- run `waystone init` first"))?;
    let vault = Vault::unlock_with_passphrase(passphrase, &keys_data)?;
    Ok((vault, dav))
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::conflict::DeviceHead;
    use waystone_core::model::{Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId};

    fn make_test_save(local_content: &[u8], mtime: &str) -> NormalizedSave {
        NormalizedSave {
            id: SaveId {
                source: "jksv".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: "GAME_001".into(),
                    display_name: "Test Game".into(),
                    confidence: Confidence::Strong,
                    title_id: Some("GAME_001".into()),
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: "switch/GAME_001/main".into(),
            portable: true,
            mtime: mtime.into(),
            files: vec![("save.dat".into(), local_content.to_vec())],
        }
    }

    #[test]
    fn build_conflict_entry_returns_some_on_diverged_heads() {
        let save = make_test_save(b"local-data", "2026-01-02T00:00:00Z");
        let heads = vec![
            DeviceHead {
                device_id: "this-dev".into(),
                hash: "old_hash".into(),
                mtime: "2026-01-01T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "other-dev".into(),
                hash: "remote_hash_abc".into(),
                mtime: "2026-01-03T00:00:00Z".into(),
            },
        ];

        let decision = SyncDecision::ConflictNeedsInput {
            local_hash: "local_computed_hash".into(),
            remote_hash: "remote_hash_abc".into(),
        };
        let entry = build_conflict_entry(
            &save,
            &decision,
            "local_computed_hash",
            "2026-01-02T00:00:00Z",
            &heads,
            "this-dev",
            0,
            "Switch JKSV",
        );

        assert!(entry.is_some());
        let entry = entry.unwrap();
        assert_eq!(entry.target_id, 0);
        assert_eq!(entry.save_key, "switch/GAME_001/main");
        assert_eq!(entry.remote_hash, "remote_hash_abc");
        assert_eq!(entry.remote.device_id, Some("other-dev".into()));
        assert_eq!(entry.remote.hash, "remote_hash_abc");
        assert_eq!(entry.local.device_id, None);
        assert_eq!(entry.local.hash, "local_computed_hash");
        assert_eq!(entry.local.mtime, "2026-01-02T00:00:00Z");
    }

    #[test]
    fn build_conflict_entry_returns_none_when_in_sync() {
        let save = make_test_save(b"same-data", "2026-01-01T00:00:00Z");
        let heads = vec![DeviceHead {
            device_id: "this-dev".into(),
            hash: "some_hash".into(),
            mtime: "2026-01-01T00:00:00Z".into(),
        }];

        let decision = SyncDecision::InSync;
        let entry = build_conflict_entry(
            &save,
            &decision,
            "some_hash",
            "2026-01-01T00:00:00Z",
            &heads,
            "this-dev",
            0,
            "Target",
        );

        assert!(entry.is_none());
    }

    #[test]
    fn build_conflict_entry_returns_none_on_clean_pull() {
        let save = make_test_save(b"old-data", "2026-01-01T00:00:00Z");
        let heads = vec![
            DeviceHead {
                device_id: "this-dev".into(),
                hash: "old_hash".into(),
                mtime: "2026-01-01T00:00:00Z".into(),
            },
            DeviceHead {
                device_id: "other-dev".into(),
                hash: "newer_hash".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
            },
        ];

        let decision = SyncDecision::Pull {
            head_hash: "newer_hash".into(),
        };
        let entry = build_conflict_entry(
            &save,
            &decision,
            "old_hash",
            "2026-01-01T00:00:00Z",
            &heads,
            "this-dev",
            0,
            "Target",
        );

        // local == base, remote is newer: this is a clean Pull, not a conflict
        assert!(entry.is_none());
    }
}
