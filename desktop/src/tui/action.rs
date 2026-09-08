use crate::config::{SyncTarget, WaystoneConfig};
use crate::helpers;
use crate::tui::app::{
    ActionResult, ConflictEntry, HeadInfo, HistoryView, Msg, SessionCreds, SetupOk, SnapshotView,
    TargetStatus,
};
use crate::webdav::{BlockingWebDav, WebDavClient};
use anyhow::Result;
use std::sync::Arc;
use tokio::sync::mpsc;
use waystone_core::conflict::{self, ConflictWinner, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_core::packaging;
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

    let saves: Vec<NormalizedSave> = load_target_saves(target)?;
    let count = saves.len();

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("pushing {} save(s)", count),
        })
        .await;

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let device_id = config.device_id.clone();

    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
        for save in &saves {
            waystone_sync::push_one(&vault, save, &device_id, dav.as_ref())?;
        }
        Ok(())
    })
    .await??;

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
    safety_backup: bool,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: "reading local tree".into(),
        })
        .await;

    let dest = target.path.clone();
    let saves: Vec<NormalizedSave> = load_target_saves(target)?;
    let count = saves.len();

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("checking {} save(s)", count),
        })
        .await;

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let device_id = config.device_id.clone();
    let adapter_name = target.adapter.clone();
    let system_name = target.system.clone();
    let policy = config.conflict_policy;

    let pulled = tokio::task::spawn_blocking(move || -> anyhow::Result<usize> {
        let mut pulled = 0usize;
        for save in &saves {
            let all_heads = waystone_sync::read_remote_heads(&vault, save, dav.as_ref())?;
            let (entry, _) = packaging::package(save);
            let decision = conflict::decide_pull(
                Some(&entry.content.hash),
                &entry.mtime,
                &all_heads,
                &device_id,
                policy,
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
                helpers::guarded_restore(
                    &vault,
                    dav.as_ref(),
                    save,
                    &hash,
                    &dest,
                    &adapter_name,
                    &system_name,
                    safety_backup,
                )?;
                pulled += 1;
            }
        }
        Ok(pulled)
    })
    .await??;

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

    let saves: Vec<NormalizedSave> = load_target_saves(target)?;

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let device_id = config.device_id.clone();
    let target_name = target.name.clone();

    let (worst, conflict_entries) = tokio::task::spawn_blocking(
        move || -> anyhow::Result<(TargetStatus, Vec<ConflictEntry>)> {
            let mut worst = TargetStatus::InSync;
            let mut conflict_entries: Vec<ConflictEntry> = Vec::new();

            for save in &saves {
                let all_heads = waystone_sync::read_remote_heads(&vault, save, dav.as_ref())?;
                let (entry, _) = packaging::package(save);

                let decision = conflict::decide_pull(
                    Some(&entry.content.hash),
                    &entry.mtime,
                    &all_heads,
                    &device_id,
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
                    &device_id,
                    target_id,
                    &target_name,
                ) {
                    conflict_entries.push(ce);
                }
            }
            Ok((worst, conflict_entries))
        },
    )
    .await??;

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
    tx: mpsc::Sender<Msg>,
    target_id: usize,
    safety_backup: bool,
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
        .ok_or_else(|| anyhow::anyhow!("save '{}' not found in target", save_key))?
        .clone();

    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!(
                "pulling blob {}...",
                &remote_hash[..12.min(remote_hash.len())]
            ),
        })
        .await;

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let hash = remote_hash.to_string();
    let dest = target.path.clone();
    let adapter_name = target.adapter.clone();
    let system_name = target.system.clone();
    let save_key_owned = save_key.to_string();

    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
        helpers::guarded_restore(
            &vault,
            dav.as_ref(),
            &save,
            &hash,
            &dest,
            &adapter_name,
            &system_name,
            safety_backup,
        )?;
        Ok(())
    })
    .await??;

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("kept remote for {}", save_key_owned)),
        })
        .await;

    Ok(())
}

pub async fn load_history_target(
    target: &SyncTarget,
    creds: &SessionCreds,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: "loading history".into(),
        })
        .await;

    let saves: Vec<NormalizedSave> = load_target_saves(target)?;

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let target_name = target.name.clone();

    let entries = tokio::task::spawn_blocking(move || -> anyhow::Result<Vec<HistoryView>> {
        let mut result: Vec<HistoryView> = Vec::new();
        for save in &saves {
            let history = waystone_sync::list_history(&vault, save, dav.as_ref())?;
            for entry in history {
                result.push(HistoryView {
                    target_id,
                    save_key: save.group_key.clone(),
                    label: format!(
                        "{} \u{00b7} {}/{}",
                        target_name, save.id.game.display_name, save.id.slot
                    ),
                    timestamp: entry.timestamp,
                    device_id: entry.device_id,
                    hash: entry.hash,
                    mtime: entry.mtime,
                });
            }
        }
        Ok(result)
    })
    .await??;

    let _ = tx.send(Msg::History { entries }).await;
    Ok(())
}

#[allow(clippy::too_many_arguments)]
pub async fn restore_history(
    target: &SyncTarget,
    save_key: String,
    hash: String,
    creds: &SessionCreds,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
    safety_backup: bool,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("restoring {}...", &hash[..12.min(hash.len())]),
        })
        .await;

    let saves: Vec<NormalizedSave> = load_target_saves(target)?;
    let save = saves
        .iter()
        .find(|s| s.group_key == save_key)
        .ok_or_else(|| anyhow::anyhow!("save '{}' not found in target", save_key))?
        .clone();

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let dest = target.path.clone();
    let adapter_name = target.adapter.clone();
    let system_name = target.system.clone();
    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
        helpers::guarded_restore(
            &vault,
            dav.as_ref(),
            &save,
            &hash,
            &dest,
            &adapter_name,
            &system_name,
            safety_backup,
        )?;
        Ok(())
    })
    .await??;

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("restored history entry for {}", save_key)),
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
    mut secret: String,
    webdav_password: Option<String>,
    recovery: bool,
    tx: mpsc::Sender<Msg>,
) {
    let result = try_unlock(&server_url, username, &secret, webdav_password, recovery).await;
    secret.zeroize();
    match result {
        Ok((vault, dav, blocking_dav)) => {
            let _ = tx
                .send(Msg::UnlockOk {
                    vault: Arc::new(vault),
                    dav: Arc::new(dav),
                    blocking_dav: Arc::new(blocking_dav),
                })
                .await;
        }
        Err(e) => {
            let _ = tx.send(Msg::UnlockFailed(e.to_string())).await;
        }
    }
    // webdav_password moved into WebDavClient (zeroized on its drop); secret zeroized above
}

async fn try_unlock(
    server_url: &str,
    username: Option<String>,
    secret: &str,
    webdav_password: Option<String>,
    recovery: bool,
) -> Result<(Vault, WebDavClient, BlockingWebDav)> {
    let dav = WebDavClient::new(server_url, username.clone(), webdav_password.clone());
    let keys_data = dav
        .get("/keys.json")
        .await?
        .ok_or_else(|| anyhow::anyhow!("no keys.json on server -- run `waystone init` first"))?;
    let vault = if recovery {
        Vault::unlock_with_recovery(secret, &keys_data)?
    } else {
        Vault::unlock_with_passphrase(secret, &keys_data)?
    };
    let server_url_owned = server_url.to_owned();
    let blocking_dav = tokio::task::spawn_blocking(move || {
        BlockingWebDav::new(&server_url_owned, username, webdav_password)
    })
    .await?;
    Ok((vault, dav, blocking_dav))
}

pub async fn run_setup(
    server_url: String,
    username: String,
    mut password: String,
    mut passphrase: String,
    tx: mpsc::Sender<Msg>,
) {
    let result = try_setup(&server_url, &username, &password, &passphrase).await;
    passphrase.zeroize();
    password.zeroize();

    match result {
        Ok((vault, dav, blocking_dav, recovery_key)) => {
            let _ = tx
                .send(Msg::SetupResult(Ok(SetupOk {
                    recovery_key,
                    vault: Arc::new(vault),
                    dav: Arc::new(dav),
                    blocking_dav: Arc::new(blocking_dav),
                })))
                .await;
        }
        Err(e) => {
            let _ = tx.send(Msg::SetupResult(Err(e.to_string()))).await;
        }
    }
}

async fn try_setup(
    server_url: &str,
    username: &str,
    password: &str,
    passphrase: &str,
) -> Result<(Vault, WebDavClient, BlockingWebDav, String)> {
    let user = if username.is_empty() {
        None
    } else {
        Some(username.to_owned())
    };
    let pass = if password.is_empty() {
        None
    } else {
        Some(password.to_owned())
    };
    let dav = WebDavClient::new(server_url, user.clone(), pass.clone());

    // Overwrite guard: refuse if keys.json already exists
    let existing = dav.get("/keys.json").await?;
    if existing.is_some() {
        anyhow::bail!("A vault already exists on this server. Use Unlock instead of Setup.");
    }

    let (vault, recovery_key) = Vault::init(passphrase)?;
    let keys_json = vault.keys_json()?;

    dav.mkdir_p("/").await?;
    dav.put("/keys.json", keys_json).await?;

    let server_url_owned = server_url.to_owned();
    let blocking_dav =
        tokio::task::spawn_blocking(move || BlockingWebDav::new(&server_url_owned, user, pass))
            .await?;
    Ok((vault, dav, blocking_dav, recovery_key))
}

pub fn save_recovery_file(key: &str, device_id: &str) -> Result<std::path::PathBuf> {
    let dir = crate::config::WaystoneConfig::config_dir()?;
    std::fs::create_dir_all(&dir)?;
    let path = dir.join(format!("recovery-{}.txt", device_id));
    #[cfg(unix)]
    {
        use std::io::Write;
        use std::os::unix::fs::OpenOptionsExt;
        let mut file = std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .mode(0o600)
            .open(&path)?;
        file.write_all(key.as_bytes())?;
    }
    #[cfg(not(unix))]
    {
        std::fs::write(&path, key)?;
    }
    Ok(path)
}

pub async fn load_snapshots_target(
    target: &SyncTarget,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: "loading snapshots".into(),
        })
        .await;

    let saves: Vec<waystone_core::model::NormalizedSave> = load_target_saves(target)?;
    let target_name = target.name.clone();

    let entries = tokio::task::spawn_blocking(move || -> anyhow::Result<Vec<SnapshotView>> {
        let mut result: Vec<SnapshotView> = Vec::new();
        for save in &saves {
            let snaps = helpers::list_snapshots(&save.group_key)?;
            for snap in snaps {
                result.push(SnapshotView {
                    target_id,
                    save_key: save.group_key.clone(),
                    label: format!(
                        "{} \u{00b7} {}/{}",
                        target_name, save.id.game.display_name, save.id.slot
                    ),
                    timestamp: snap.timestamp,
                    file_count: snap.file_count,
                    total_bytes: snap.total_bytes,
                });
            }
        }
        Ok(result)
    })
    .await??;

    let _ = tx.send(Msg::Snapshots { entries }).await;
    Ok(())
}

pub async fn restore_snapshot(
    target: &SyncTarget,
    save_key: String,
    timestamp: String,
    tx: mpsc::Sender<Msg>,
    target_id: usize,
    safety_backup: bool,
) -> Result<()> {
    let _ = tx
        .send(Msg::Progress {
            target_id,
            phase: format!("restoring snapshot {}...", timestamp),
        })
        .await;

    let dest = target.path.clone();
    let save_key_owned = save_key.clone();
    let timestamp_owned = timestamp.clone();
    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
        helpers::restore_from_snapshot(&save_key_owned, &timestamp_owned, &dest, safety_backup)?;
        Ok(())
    })
    .await??;

    let _ = tx
        .send(Msg::ActionDone {
            target_id,
            result: ActionResult::Ok(format!("restored snapshot {} for {}", timestamp, save_key)),
        })
        .await;
    Ok(())
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
