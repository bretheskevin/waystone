use crate::config::{SyncTarget, WaystoneConfig};
use crate::helpers;
use crate::keychain::SecretStore;
use crate::sync_run::{self, NoProgress, RunMode, Selection};
use crate::tui::app::{
    ActionResult, ConflictEntry, HeadInfo, HistoryView, Msg, SaveRow, SessionCreds, SetupOk,
    SnapshotView, TargetStatus,
};
use crate::webdav::{BlockingWebDav, WebDavClient, make_blocking_dav};
use anyhow::Result;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use tokio::sync::mpsc;
use waystone_core::conflict::{self, ConflictPolicy, SyncDecision};
use waystone_core::crypto::Vault;
use waystone_core::model::NormalizedSave;
use waystone_core::url::redact_userinfo;
use waystone_sync::engine::{
    self, PlanAction, PlanEntry, SyncPhase, SyncProgressSink, SyncSummary,
};
use zeroize::Zeroize;

/// Streams engine progress to the UI; runs on the blocking thread (`blocking_send`).
pub struct ChannelSink {
    pub tx: mpsc::Sender<Msg>,
    pub cancel: Arc<AtomicBool>,
}

impl SyncProgressSink for ChannelSink {
    fn started(&self, total: usize) {
        let _ = self.tx.blocking_send(Msg::SyncStarted { total });
    }

    fn item(&self, index: usize, total: usize, save: &NormalizedSave, phase: SyncPhase) {
        let _ = self.tx.blocking_send(Msg::SyncItem {
            index,
            total,
            name: save.id.game.display_name.clone(),
            phase,
        });
    }

    fn is_cancelled(&self) -> bool {
        self.cancel.load(Ordering::SeqCst)
    }
}

pub fn failed_summary(message: String) -> SyncSummary {
    SyncSummary {
        errors: 1,
        error_messages: vec![message],
        ..Default::default()
    }
}

pub struct SyncCtx {
    pub creds: SessionCreds,
    pub targets: Vec<SyncTarget>,
    pub config: WaystoneConfig,
    pub cancel: Arc<AtomicBool>,
}

/// Plan + execute `selection` through the shared engine, streaming progress; always ends with `SyncFinished`.
pub async fn run_sync(selection: Selection, mode: RunMode, ctx: SyncCtx, tx: mpsc::Sender<Msg>) {
    log::info!("[sync] tui run start: {selection:?} {mode:?}");
    let sink = ChannelSink {
        tx: tx.clone(),
        cancel: ctx.cancel.clone(),
    };
    let result = tokio::task::spawn_blocking(move || -> Result<SyncSummary> {
        let vault = ctx.creds.vault;
        let dav = ctx.creds.blocking_dav;
        let dav_ref: &dyn waystone_sync::WebDav = dav.as_ref();
        let remote = sync_run::wants_remote_only(mode).then_some((vault.as_ref(), dav_ref));
        let collected = sync_run::collect_saves(&ctx.targets, &selection, remote)?;
        let planned = sync_run::plan_run(
            &vault,
            collected,
            &ctx.config.device_id,
            ctx.config.conflict_policy,
            mode,
            dav_ref,
        );
        Ok(sync_run::execute_run(
            &vault,
            &planned,
            &ctx.targets,
            &ctx.config.device_id,
            ctx.config.safety_backup,
            helpers::default_backups_root()?,
            dav_ref,
            &sink,
        ))
    })
    .await;
    let summary = match result {
        Ok(Ok(s)) => s,
        Ok(Err(e)) => {
            log::warn!("[sync] tui run failed: {e:#}");
            failed_summary(format!("{e:#}"))
        }
        Err(e) => {
            log::warn!("[sync] tui run task died: {e}");
            failed_summary(format!("sync task failed: {e}"))
        }
    };
    log::info!("[sync] tui run done: {}", summary.headline());
    let _ = tx.send(Msg::SyncFinished(summary)).await;
}

fn status_of(entry: &PlanEntry) -> TargetStatus {
    match entry.action {
        PlanAction::InSync => TargetStatus::InSync,
        PlanAction::Push => TargetStatus::Ahead,
        PlanAction::Pull => TargetStatus::Behind,
        PlanAction::Conflict => TargetStatus::Conflict,
        PlanAction::Error => TargetStatus::Error(entry.error.clone().unwrap_or_default()),
    }
}

pub fn conflict_of(
    e: &PlanEntry,
    device_id: &str,
    target_id: usize,
    target_name: &str,
) -> Option<ConflictEntry> {
    build_conflict_entry(
        &e.save,
        e.decision.as_ref()?,
        e.local_hash.as_deref().unwrap_or_default(),
        &e.save.mtime,
        &e.heads,
        device_id,
        target_id,
        target_name,
    )
}

/// Per-save rows + conflict inbox entries from a decided plan (all entries belong to `target_id`).
pub fn rows_from_plan(
    entries: &[PlanEntry],
    device_id: &str,
    target_id: usize,
    target_name: &str,
) -> (Vec<SaveRow>, Vec<ConflictEntry>) {
    let rows = entries
        .iter()
        .map(|e| SaveRow {
            target_id,
            group_key: e.save.group_key.clone(),
            name: e.save.id.game.display_name.clone(),
            slot: e.save.id.slot.clone(),
            status: status_of(e),
            remote_only: engine::is_remote_only(&e.save),
        })
        .collect();
    let conflicts = entries
        .iter()
        .filter_map(|e| conflict_of(e, device_id, target_id, target_name))
        .collect();
    (rows, conflicts)
}

/// Scans one target (local + remote-only) and reports per-save status rows.
pub async fn scan_target(
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
    log::info!("[sync] tui scan target {target_id} ({})", target.name);

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let device_id = config.device_id.clone();
    let owner = target.clone();
    let target = target.clone();

    let (rows, conflicts, scan_errors) = tokio::task::spawn_blocking(move || -> Result<_> {
        let dav_ref: &dyn waystone_sync::WebDav = dav.as_ref();
        let targets = [target];
        let collected = sync_run::collect_saves(
            &targets,
            &Selection::Target(0),
            Some((vault.as_ref(), dav_ref)),
        )?;
        let scan_errors = collected.scan_errors.clone();
        let saves: Vec<NormalizedSave> = collected.saves.into_iter().map(|(_, s)| s).collect();
        let entries = engine::plan(&vault, &saves, &device_id, ConflictPolicy::Prompt, dav_ref);
        let (rows, conflicts) = rows_from_plan(&entries, &device_id, target_id, &targets[0].name);
        Ok((rows, conflicts, scan_errors))
    })
    .await??;

    if let Some(err) = scan_errors.into_iter().next() {
        let _ = tx
            .send(Msg::Status {
                target_id,
                status: TargetStatus::Error(err),
            })
            .await;
        return Ok(());
    }
    let _ = tx
        .send(Msg::Rows {
            target_id,
            target: owner,
            rows,
            conflicts,
        })
        .await;
    Ok(())
}

/// "<target> · <game>/<slot>" row label shared by conflicts, history and snapshots.
fn save_label(target_name: &str, save: &NormalizedSave) -> String {
    format!(
        "{target_name} \u{00b7} {}/{}",
        save.id.game.display_name, save.id.slot
    )
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
                label: save_label(target_name, save),
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
                head_mtime: winning_head.mtime.clone(),
                queued: false,
            })
        }
        _ => None,
    }
}

/// Streams every conflict (Prompt policy) to the inbox as each save is planned.
pub async fn scan_conflicts(
    targets: Vec<SyncTarget>,
    creds: &SessionCreds,
    config: &WaystoneConfig,
    tx: mpsc::Sender<Msg>,
) {
    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let device_id = config.device_id.clone();
    let sink = tx.clone();
    let result = tokio::task::spawn_blocking(move || -> Result<Option<String>> {
        let dav_ref: &dyn waystone_sync::WebDav = dav.as_ref();
        let collected = sync_run::collect_saves(&targets, &Selection::All, None)?;
        for (idx, save) in &collected.saves {
            let entry = engine::plan_one(&vault, save, &device_id, ConflictPolicy::Prompt, dav_ref);
            if let Some(c) = conflict_of(&entry, &device_id, *idx, &targets[*idx].name) {
                let _ = sink.blocking_send(Msg::ConflictFound(c));
            }
        }
        Ok(collected.scan_errors.into_iter().next())
    })
    .await;
    let error = match result {
        Ok(Ok(e)) => e,
        Ok(Err(e)) => Some(format!("{e:#}")),
        Err(e) => Some(format!("scan task failed: {e}")),
    };
    log::info!("[sync] conflict scan done (error={error:?})");
    let _ = tx.send(Msg::ConflictScanDone { error }).await;
}

/// Forced push of ONLY `save_key`.
pub async fn resolve_keep_local(
    target: &SyncTarget,
    save_key: &str,
    creds: &SessionCreds,
    config: &WaystoneConfig,
) -> Result<()> {
    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let targets = vec![target.clone()];
    let selection = Selection::Save {
        target: 0,
        group_key: save_key.to_string(),
    };
    let device_id = config.device_id.clone();
    let summary = tokio::task::spawn_blocking(move || -> Result<SyncSummary> {
        let dav_ref: &dyn waystone_sync::WebDav = dav.as_ref();
        let collected = sync_run::collect_saves(&targets, &selection, None)?;
        let planned = sync_run::plan_run(
            &vault,
            collected,
            &device_id,
            ConflictPolicy::Prompt,
            RunMode::Push,
            dav_ref,
        );
        Ok(sync_run::execute_run(
            &vault,
            &planned,
            &targets,
            &device_id,
            false,
            std::path::PathBuf::new(),
            dav_ref,
            &NoProgress,
        ))
    })
    .await??;
    if summary.pushed == 0 && summary.errors == 0 {
        log::warn!("[sync] keep-local {save_key}: save not found locally, nothing pushed");
        anyhow::bail!("save {save_key} not found locally");
    }
    if summary.errors > 0 {
        anyhow::bail!(summary.error_messages.join("; "));
    }
    Ok(())
}

pub async fn resolve_keep_remote(
    target: &SyncTarget,
    save_key: &str,
    remote_hash: &str,
    head_mtime: &str,
    creds: &SessionCreds,
    config: &WaystoneConfig,
) -> Result<()> {
    let safety_backup = config.safety_backup;
    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let hash = remote_hash.to_string();
    let head_mtime = head_mtime.to_string();
    let target = target.clone();
    let key = save_key.to_string();
    let device_id = config.device_id.clone();
    tokio::task::spawn_blocking(move || {
        crate::save_io::restore_one(&target, &key, safety_backup, |save, io| {
            engine::pull_hash(
                &vault,
                0,
                save,
                &hash,
                &head_mtime,
                &device_id,
                dav.as_ref(),
                io,
            )
        })
    })
    .await??;
    Ok(())
}

pub async fn resolve_job(
    job: crate::tui::app::ResolveJob,
    targets: &[SyncTarget],
    creds: &SessionCreds,
    config: &WaystoneConfig,
) -> Result<()> {
    use crate::tui::app::ResolveJob;
    let target_id = job.key().0;
    let target = targets
        .get(target_id)
        .ok_or_else(|| anyhow::anyhow!("no target at index {target_id}"))?;
    match job {
        ResolveJob::KeepLocal { group_key, .. } => {
            resolve_keep_local(target, &group_key, creds, config).await
        }
        ResolveJob::KeepRemote {
            group_key,
            hash,
            head_mtime,
            ..
        } => resolve_keep_remote(target, &group_key, &hash, &head_mtime, creds, config).await,
    }
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

    let saves: Vec<NormalizedSave> = helpers::load_target_saves(target)?;

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
                    label: save_label(&target_name, save),
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

    let _ = tx
        .send(Msg::History {
            target_id,
            target: target.clone(),
            entries,
        })
        .await;
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

    let vault = Arc::clone(&creds.vault);
    let dav = Arc::clone(&creds.blocking_dav);
    let target = target.clone();
    let key = save_key.clone();
    tokio::task::spawn_blocking(move || {
        crate::save_io::restore_one(&target, &key, safety_backup, |save, io| {
            engine::restore_hash(&vault, 0, save, &hash, dav.as_ref(), io)
        })
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

/// Rebuilds the session from the keychain (no prompt); None when absent/other server/unusable.
pub async fn restore_session(
    store: &dyn SecretStore,
    server_url: &str,
    config_username: Option<String>,
) -> Option<SessionCreds> {
    let (vault, session) = crate::cli_session::keychain_vault(store, server_url)?;
    let (username, stored) = crate::cli_session::session_identity(&session, config_username);
    let password = crate::cli_session::env_webdav_password().or(stored);
    if username.is_some() && password.is_none() {
        log::info!("[vault] keychain session has no WebDAV password for {username:?}; prompting");
        return None;
    }
    let blocking = match make_blocking_dav(server_url, username.clone(), password.clone()).await {
        Ok(b) => b,
        Err(e) => {
            log::warn!("[vault] keychain session restore: WebDAV client failed ({e:#})");
            return None;
        }
    };
    Some(SessionCreds {
        vault: Arc::new(vault),
        dav: Arc::new(WebDavClient::new(server_url, username, password)),
        blocking_dav: Arc::new(blocking),
    })
}

/// New WebDAV clients with `password`; persists it in the keychain session.
pub async fn rebuild_creds(
    server_url: &str,
    username: Option<String>,
    mut password: String,
    vault: &Vault,
    store: &dyn SecretStore,
    tx: mpsc::Sender<Msg>,
) {
    match make_blocking_dav(server_url, username.clone(), Some(password.clone())).await {
        Ok(blocking) => {
            log::info!("[vault] WebDAV clients rebuilt with the new password");
            let dav = WebDavClient::new(server_url, username.clone(), Some(password.clone()));
            crate::cli_session::remember(
                store,
                server_url,
                username,
                vault,
                Some(password.clone()),
            );
            let _ = tx
                .send(Msg::CredsRebuilt {
                    dav: Arc::new(dav),
                    blocking_dav: Arc::new(blocking),
                })
                .await;
        }
        Err(e) => log::warn!("[vault] rebuilding WebDAV client failed: {e:#}"),
    }
    password.zeroize();
}

pub async fn check_update(tx: mpsc::Sender<Msg>) {
    let result = tokio::task::spawn_blocking(|| crate::updater::check(crate::APP_VERSION)).await;
    let result = result.unwrap_or(Err(crate::updater::UpdateError::Parse));
    let _ = tx.send(Msg::UpdateChecked(result)).await;
}

pub async fn install_update(rel: crate::updater::Release, tx: mpsc::Sender<Msg>) {
    let sink = tx.clone();
    let version = rel.version.clone();
    let result = tokio::task::spawn_blocking(move || {
        crate::updater::install(&rel, |done, total| {
            let _ = sink.blocking_send(Msg::UpdateProgress { done, total });
        })
    })
    .await
    .unwrap_or(Err(crate::updater::UpdateError::Install));
    let _ = tx
        .send(Msg::UpdateInstalled(result.map(|()| version)))
        .await;
}

pub async fn attempt_unlock(
    server_url: String,
    username: Option<String>,
    mut secret: String,
    webdav_password: Option<String>,
    recovery: bool,
    tx: mpsc::Sender<Msg>,
    store: Arc<dyn SecretStore>,
) {
    let result = try_unlock(
        &server_url,
        username.clone(),
        &secret,
        webdav_password.clone(),
        recovery,
    )
    .await;
    secret.zeroize();
    match result {
        Ok((vault, dav, blocking_dav)) => {
            log::info!("[vault] tui unlock ok (recovery={recovery})");
            crate::cli_session::remember(
                store.as_ref(),
                &server_url,
                username,
                &vault,
                webdav_password,
            );
            let _ = tx
                .send(Msg::UnlockOk {
                    vault: Arc::new(vault),
                    dav: Arc::new(dav),
                    blocking_dav: Arc::new(blocking_dav),
                })
                .await;
        }
        Err(e) => {
            log::warn!("[vault] tui unlock failed (recovery={recovery}): {e:#}");
            let _ = tx.send(Msg::UnlockFailed(e.to_string())).await;
        }
    }
}

async fn try_unlock(
    server_url: &str,
    username: Option<String>,
    secret: &str,
    webdav_password: Option<String>,
    recovery: bool,
) -> Result<(Vault, WebDavClient, BlockingWebDav)> {
    let vault = crate::cli_session::unlock_from_server(
        server_url,
        &username,
        &webdav_password,
        secret,
        recovery,
    )
    .await?;
    let dav = WebDavClient::new(server_url, username.clone(), webdav_password.clone());
    let blocking_dav = make_blocking_dav(server_url, username, webdav_password).await?;
    Ok((vault, dav, blocking_dav))
}

pub async fn run_setup(
    server_url: String,
    username: String,
    mut password: String,
    mut passphrase: String,
    tx: mpsc::Sender<Msg>,
    store: Arc<dyn SecretStore>,
) {
    let result = try_setup(&server_url, &username, &password, &passphrase).await;
    passphrase.zeroize();
    let remembered_password = (!password.is_empty()).then(|| password.clone());
    password.zeroize();

    match result {
        Ok((vault, dav, blocking_dav, recovery_key)) => {
            log::info!("[vault] tui setup ok for {}", redact_userinfo(&server_url));
            crate::cli_session::remember(
                store.as_ref(),
                &server_url,
                (!username.is_empty()).then(|| username.clone()),
                &vault,
                remembered_password,
            );
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
            log::warn!(
                "[vault] tui setup failed for {}: {e:#}",
                redact_userinfo(&server_url)
            );
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

    let (vault, recovery_key) = crate::vault_ops::init_vault(&dav, passphrase).await?;

    let blocking_dav = make_blocking_dav(server_url, user, pass).await?;
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

    let saves: Vec<waystone_core::model::NormalizedSave> = helpers::load_target_saves(target)?;
    let target_name = target.name.clone();

    let entries = tokio::task::spawn_blocking(move || -> anyhow::Result<Vec<SnapshotView>> {
        let mut result: Vec<SnapshotView> = Vec::new();
        for save in &saves {
            let snaps = helpers::list_snapshots(&save.group_key)?;
            for snap in snaps {
                result.push(SnapshotView {
                    target_id,
                    save_key: save.group_key.clone(),
                    label: save_label(&target_name, save),
                    timestamp: snap.timestamp,
                    file_count: snap.file_count,
                    total_bytes: snap.total_bytes,
                });
            }
        }
        Ok(result)
    })
    .await??;

    let _ = tx
        .send(Msg::Snapshots {
            target_id,
            target: target.clone(),
            entries,
        })
        .await;
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

    let target = target.clone();
    let save_key_owned = save_key.clone();
    let timestamp_owned = timestamp.clone();
    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
        let rt = helpers::restore_target_for(&target, &save_key_owned)?;
        helpers::restore_from_snapshot(&save_key_owned, &timestamp_owned, &rt, safety_backup)?;
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

    fn entry(action: PlanAction, remote_only: bool) -> PlanEntry {
        let mut save = make_test_save(b"x", "2026-01-01T00:00:00Z");
        if remote_only {
            save.files.clear();
        }
        let mut e = engine::forced_push(make_test_save(b"x", "")).unwrap();
        e.save = save;
        e.action = action;
        if action == PlanAction::Error {
            e.error = Some("boom".into());
        }
        e
    }

    #[test]
    fn rows_from_plan_maps_actions_to_statuses_and_flags_remote_only() {
        let entries = [
            entry(PlanAction::InSync, false),
            entry(PlanAction::Push, false),
            entry(PlanAction::Pull, true),
            entry(PlanAction::Conflict, false),
            entry(PlanAction::Error, false),
        ];
        let (rows, conflicts) = rows_from_plan(&entries, "dev", 3, "T");
        let statuses: Vec<_> = rows.iter().map(|r| r.status.clone()).collect();
        assert_eq!(
            statuses,
            vec![
                TargetStatus::InSync,
                TargetStatus::Ahead,
                TargetStatus::Behind,
                TargetStatus::Conflict,
                TargetStatus::Error("boom".into())
            ]
        );
        assert!(rows[2].remote_only && !rows[0].remote_only);
        assert!(rows.iter().all(|r| r.target_id == 3));
        assert!(conflicts.is_empty());
    }

    #[tokio::test(flavor = "multi_thread")]
    async fn restore_session_uses_keychain_only_for_the_same_server() {
        use crate::keychain::MemoryStore;
        let store = MemoryStore::default();
        let vault = Vault::init("pw").unwrap().0;
        crate::cli_session::remember(&store, "http://nopw", Some("bob".into()), &vault, None);
        assert!(
            restore_session(&store, "http://nopw", None).await.is_none(),
            "a user without a stored WebDAV password must be prompted"
        );
        crate::cli_session::remember(
            &store,
            "http://srv",
            Some("bob".into()),
            &vault,
            Some("pw".into()),
        );
        let creds = restore_session(&store, "http://srv", None)
            .await
            .expect("session restored");
        assert_eq!(creds.vault.export_mdk(), vault.export_mdk());
        tokio::task::spawn_blocking(move || drop(creds))
            .await
            .unwrap();
        assert!(
            restore_session(&store, "http://other", None)
                .await
                .is_none()
        );
        assert!(
            restore_session(&store, "http://srv", None).await.is_none(),
            "stale entry cleared"
        );
    }
}
