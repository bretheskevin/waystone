use crate::config::{SyncTarget, WaystoneConfig};
use crate::helpers;
use crate::pipeline::SyncPipeline;
use crate::tui::app::{ActionResult, Msg, SessionCreds, TargetStatus};
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
/// `Box\<dyn Adapter\>` is `!Send`. The adapter is created and dropped inside
/// this function so the returned `Vec\<NormalizedSave\>` is `Send` and callers
/// can hold it across `.await` points without triggering a `!Send` error.
fn load_target_saves(target: &SyncTarget) -> anyhow::Result<Vec<NormalizedSave>> {
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
            let files = waystone_core::packaging::unzip(&zip_bytes)?;

            // Write files — use adapter to convert back to native layout
            let system = helpers::parse_system(&target.system)?;
            let adapter = helpers::make_adapter(&target.adapter, system)?;
            let mut restored = save.clone();
            restored.files = files;
            let native = adapter.to_native(&restored);
            for file in &native.files {
                let path = dest.join(&file.path);
                if let Some(parent) = path.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                std::fs::write(&path, &file.content)?;
            }
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
    for save in &saves {
        let all_heads = pipe.read_all_remote_heads(save).await?;
        let (entry, _) = waystone_core::packaging::package(save);
        let decision = conflict::decide_pull(
            Some(&entry.content.hash),
            &entry.mtime,
            &all_heads,
            &config.device_id,
            config.conflict_policy,
        );
        let status = match decision {
            SyncDecision::InSync => TargetStatus::InSync,
            SyncDecision::Pull { .. } => TargetStatus::Behind,
            SyncDecision::Push => TargetStatus::Ahead,
            SyncDecision::ConflictResolved { .. } => TargetStatus::Conflict,
            SyncDecision::ConflictNeedsInput { .. } => TargetStatus::Conflict,
        };
        worst = worst_status(worst, status);
    }

    let _ = tx
        .send(Msg::Status {
            target_id,
            status: worst,
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
