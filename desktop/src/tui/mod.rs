pub mod action;
pub mod app;
pub mod event;
pub mod theme;
pub mod ui;

use crate::config::WaystoneConfig;
use crate::keychain::{self, OsKeychain, SecretStore};
use crate::net_status::{self, NoInternetReason};
use crate::tui::app::{App, Cmd, Msg};
use anyhow::Result;
use crossterm::ExecutableCommand;
use crossterm::event::{Event, EventStream, KeyEventKind};
use crossterm::terminal::{
    EnterAlternateScreen, LeaveAlternateScreen, disable_raw_mode, enable_raw_mode,
};
use futures::StreamExt;
use ratatui::Terminal;
use ratatui::backend::CrosstermBackend;
use std::future::Future;
use std::io::stdout;
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::Ordering;
use tokio::sync::mpsc;
use zeroize::Zeroize;

struct TerminalGuard;

impl TerminalGuard {
    fn init() -> Result<Terminal<CrosstermBackend<std::io::Stdout>>> {
        enable_raw_mode()?;
        stdout().execute(EnterAlternateScreen)?;
        let backend = CrosstermBackend::new(stdout());
        let terminal = Terminal::new(backend)?;
        Ok(terminal)
    }
}

impl Drop for TerminalGuard {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
        let _ = stdout().execute(LeaveAlternateScreen);
    }
}

pub async fn run(mut config: WaystoneConfig, config_path: PathBuf) -> Result<()> {
    let _guard = TerminalGuard;
    let mut terminal = TerminalGuard::init()?;

    let original_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let _ = disable_raw_mode();
        let _ = stdout().execute(LeaveAlternateScreen);
        original_hook(info);
    }));

    let mut app = App::new(&config);
    let store: Arc<dyn SecretStore> = Arc::new(OsKeychain);
    let (tx, mut rx) = mpsc::channel::<Msg>(64);
    let mut event_stream = EventStream::new();

    if !config.server_url.is_empty() {
        match action::restore_session(store.as_ref(), &config.server_url, config.username.clone())
            .await
        {
            Some(creds) => {
                app.creds = Some(creds);
                app.push_log("Session restored from keychain.".into());
                dispatch_cmd(
                    &mut app,
                    Cmd::RefreshAllStatuses,
                    &mut config,
                    &config_path,
                    tx.clone(),
                    &store,
                )
                .await?;
            }
            None => {
                tokio::spawn(with_online(
                    online_probe(config.server_url.clone()),
                    tx.clone(),
                    async {},
                ));
            }
        }
    }

    let mut tick_interval = tokio::time::interval(std::time::Duration::from_millis(200));

    loop {
        terminal.draw(|frame| ui::ui(frame, &app))?;

        let msg = tokio::select! {
            event = event_stream.next() => {
                match event {
                    Some(Ok(Event::Key(key))) if key.kind == KeyEventKind::Press => {
                        Some(event::map_key(key, &app).unwrap_or(Msg::Key(key)))
                    }
                    _ => None,
                }
            }
            msg = rx.recv() => msg,
            _ = tick_interval.tick() => Some(Msg::Tick),
        };

        if let Some(msg) = msg {
            let cmds = app::update(&mut app, msg);
            for cmd in cmds {
                dispatch_cmd(&mut app, cmd, &mut config, &config_path, tx.clone(), &store).await?;
            }
        }

        if app.should_quit {
            break;
        }
    }

    Ok(())
}

async fn dispatch_cmd(
    app: &mut App,
    cmd: Cmd,
    config: &mut WaystoneConfig,
    config_path: &std::path::Path,
    tx: mpsc::Sender<Msg>,
    store: &Arc<dyn SecretStore>,
) -> Result<()> {
    match cmd {
        Cmd::Quit => {
            if app.dirty {
                let cfg = app.to_config(config);
                cfg.save_to(config_path)?;
            }
            app.should_quit = true;
        }

        Cmd::SaveConfig => {
            let cfg = app.to_config(config);
            cfg.save_to(config_path)?;
            app.dirty = false;
            app.push_log("Config saved.".into());
        }

        Cmd::StartSync { selection, mode } => {
            if let Some(creds) = &app.creds {
                app.busy = Some(app::SYNC_BUSY);
                app.sync_cancel.store(false, Ordering::SeqCst);
                let ctx = action::SyncCtx {
                    creds: app::SessionCreds {
                        vault: creds.vault.clone(),
                        dav: creds.dav.clone(),
                        blocking_dav: creds.blocking_dav.clone(),
                    },
                    targets: app.targets.clone(),
                    config: config.clone(),
                    cancel: app.sync_cancel.clone(),
                };
                let tx_run = tx.clone();
                let handle = tokio::spawn(with_online(
                    online_probe(app.server_url.clone()),
                    tx.clone(),
                    async move {
                        action::run_sync(selection, mode, ctx, tx_run).await;
                    },
                ));
                watch_sync_task(handle, tx.clone());
            }
        }

        Cmd::RefreshStatus(target_id) => spawn_scans(app, config, &tx, vec![target_id]),

        Cmd::RefreshAllStatuses => {
            let ids = (0..app.targets.len()).collect();
            spawn_scans(app, config, &tx, ids);
        }

        Cmd::ScanConflicts => {
            if let Some(creds) = &app.creds {
                let creds = clone_creds(creds);
                let (targets, cfg, tx2) = (app.targets.clone(), config.clone(), tx.clone());
                tokio::spawn(with_online(
                    online_probe(app.server_url.clone()),
                    tx.clone(),
                    async move {
                        action::scan_conflicts(targets, &creds, &cfg, tx2).await;
                    },
                ));
            }
        }

        Cmd::Resolve(job) => {
            let creds = app.creds.as_ref().map(clone_creds);
            let (targets, cfg, tx2) = (app.targets.clone(), config.clone(), tx.clone());
            let probe = online_probe(app.server_url.clone());
            tokio::spawn(async move {
                let (target_id, group_key) = job.key();
                let group_key = group_key.to_string();
                let result = match creds {
                    None => Err("Locked -- unlock, then resolve again.".to_string()),
                    Some(creds) => {
                        let resolve = action::resolve_job(job, &targets, &creds, &cfg);
                        match with_online(probe, tx2.clone(), resolve).await {
                            Ok(Ok(())) => Ok(()),
                            Ok(Err(e)) => Err(format!("{e:#}")),
                            Err(reason) => Err(reason.headline().to_string()),
                        }
                    }
                };
                log::info!("[sync] tui resolve {group_key}: {result:?}");
                let _ = tx2
                    .send(Msg::ResolveDone {
                        target_id,
                        group_key,
                        result,
                    })
                    .await;
            });
        }

        Cmd::RebuildCreds { password } => {
            if let Some(creds) = &app.creds {
                let vault = creds.vault.clone();
                let (server, user) = (app.server_url.clone(), app.username.clone());
                let (store, tx2) = (store.clone(), tx.clone());
                tokio::spawn(async move {
                    action::rebuild_creds(&server, user, password, &vault, store.as_ref(), tx2)
                        .await;
                });
            }
        }

        Cmd::ClearSession => {
            // Off the UI loop: an OS keychain prompt must not freeze the redraw.
            let store = store.clone();
            tokio::task::spawn_blocking(move || {
                if let Err(e) = keychain::clear_session(store.as_ref()) {
                    log::warn!("[vault] clearing keychain session failed: {e:#}");
                }
            });
        }

        Cmd::CheckUpdate => {
            tokio::spawn(action::check_update(tx.clone()));
        }

        Cmd::InstallUpdate(rel) => {
            tokio::spawn(action::install_update(rel, tx.clone()));
        }

        Cmd::AttemptUnlock {
            passphrase,
            webdav_password,
            recovery,
        } => {
            let server_url = app.server_url.clone();
            let username = app.username.clone();
            let (tx2, store) = (tx.clone(), store.clone());
            tokio::spawn(with_online(
                online_probe(server_url.clone()),
                tx.clone(),
                async move {
                    action::attempt_unlock(
                        server_url,
                        username,
                        passphrase,
                        webdav_password,
                        recovery,
                        tx2,
                        store,
                    )
                    .await;
                },
            ));
        }

        Cmd::LoadHistory { target_id } => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                let creds = clone_creds(creds);
                let tx2 = tx.clone();
                tokio::spawn(with_online(
                    online_probe(app.server_url.clone()),
                    tx.clone(),
                    async move {
                        if let Err(e) =
                            action::load_history_target(&target, &creds, tx2.clone(), target_id)
                                .await
                        {
                            let _ = tx2
                                .send(Msg::ActionDone {
                                    target_id,
                                    result: app::ActionResult::Err(e.to_string()),
                                })
                                .await;
                        }
                    },
                ));
            }
        }

        Cmd::RestoreHistory {
            target_id,
            save_key,
            hash,
        } => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                app.busy = Some(target_id);
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let blocking_dav = creds.blocking_dav.clone();
                let safety_backup = config.safety_backup;
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    let creds = app::SessionCreds {
                        vault,
                        dav,
                        blocking_dav,
                    };
                    if let Err(e) = action::restore_history(
                        &target,
                        save_key,
                        hash,
                        &creds,
                        tx_action.clone(),
                        target_id,
                        safety_backup,
                    )
                    .await
                    {
                        let _ = tx_action
                            .send(Msg::ActionDone {
                                target_id,
                                result: app::ActionResult::Err(e.to_string()),
                            })
                            .await;
                    }
                });
                watch_busy_task(handle, tx.clone(), target_id);
            }
        }

        Cmd::RunSetup {
            server_url,
            username,
            password,
            passphrase,
        } => {
            let (tx2, store) = (tx.clone(), store.clone());
            tokio::spawn(with_online(
                online_probe(server_url.clone()),
                tx.clone(),
                async move {
                    action::run_setup(server_url, username, password, passphrase, tx2, store).await;
                },
            ));
        }

        Cmd::SaveRecoveryFile { mut key, device_id } => {
            let actual_device_id = if device_id.is_empty() {
                config.device_id.clone()
            } else {
                device_id
            };
            let result = action::save_recovery_file(&key, &actual_device_id);
            key.zeroize();
            match result {
                Ok(path) => {
                    let _ = tx.send(Msg::RecoverySaved(Ok(path))).await;
                }
                Err(e) => {
                    let _ = tx.send(Msg::RecoverySaved(Err(e.to_string()))).await;
                }
            }
        }

        Cmd::SaveSettings(new_cfg) => match new_cfg.save() {
            Ok(()) => {
                *config = new_cfg;
                app.push_log("Settings saved to disk.".into());
            }
            Err(e) => {
                app.push_log(format!("Failed to save settings: {}", e));
            }
        },

        Cmd::LoadSnapshots { target_id } => {
            if let Some(target) = app.targets.get(target_id).cloned() {
                let tx = tx.clone();
                tokio::spawn(async move {
                    if let Err(e) =
                        action::load_snapshots_target(&target, tx.clone(), target_id).await
                    {
                        let _ = tx
                            .send(Msg::ActionDone {
                                target_id,
                                result: app::ActionResult::Err(e.to_string()),
                            })
                            .await;
                    }
                });
            }
        }

        Cmd::RestoreSnapshot {
            target_id,
            save_key,
            timestamp,
        } => {
            if let Some(target) = app.targets.get(target_id).cloned() {
                app.busy = Some(target_id);
                let safety_backup = config.safety_backup;
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    if let Err(e) = action::restore_snapshot(
                        &target,
                        save_key,
                        timestamp,
                        tx_action.clone(),
                        target_id,
                        safety_backup,
                    )
                    .await
                    {
                        let _ = tx_action
                            .send(Msg::ActionDone {
                                target_id,
                                result: app::ActionResult::Err(e.to_string()),
                            })
                            .await;
                    }
                });
                watch_busy_task(handle, tx.clone(), target_id);
            }
        }
    }
    Ok(())
}

fn watch_busy_task(handle: tokio::task::JoinHandle<()>, tx: mpsc::Sender<Msg>, target_id: usize) {
    tokio::spawn(async move {
        if let Err(e) = handle.await {
            let _ = tx
                .send(Msg::ActionDone {
                    target_id,
                    result: app::ActionResult::Err(format!("task panicked: {}", e)),
                })
                .await;
        }
    });
}

fn clone_creds(c: &app::SessionCreds) -> app::SessionCreds {
    app::SessionCreds {
        vault: c.vault.clone(),
        dav: c.dav.clone(),
        blocking_dav: c.blocking_dav.clone(),
    }
}

pub fn online_probe(
    server_url: String,
) -> impl FnOnce() -> Option<NoInternetReason> + Send + 'static {
    move || net_status::probe(&server_url)
}

/// Probes first; offline -> reports it and skips `fut`, online -> clears the banner and runs it.
pub async fn with_online<F: Future>(
    probe: impl FnOnce() -> Option<NoInternetReason> + Send + 'static,
    tx: mpsc::Sender<Msg>,
    fut: F,
) -> Result<F::Output, NoInternetReason> {
    let reason = tokio::task::spawn_blocking(probe).await.unwrap_or(None);
    let _ = tx.send(Msg::Offline(reason)).await;
    match reason {
        Some(r) => Err(r),
        None => Ok(fut.await),
    }
}

fn spawn_scans(app: &app::App, config: &WaystoneConfig, tx: &mpsc::Sender<Msg>, ids: Vec<usize>) {
    let Some(creds) = &app.creds else {
        return;
    };
    let jobs: Vec<_> = ids
        .into_iter()
        .filter_map(|id| Some((id, app.targets.get(id)?.clone())))
        .collect();
    let creds = clone_creds(creds);
    let (cfg, tx2) = (config.clone(), tx.clone());
    tokio::spawn(with_online(
        online_probe(app.server_url.clone()),
        tx.clone(),
        async move {
            futures::future::join_all(jobs.iter().map(|(id, target)| {
                let (creds, cfg, tx) = (&creds, &cfg, tx2.clone());
                async move {
                    if let Err(e) = action::scan_target(target, creds, cfg, tx.clone(), *id).await {
                        log::warn!("[sync] tui scan target {id} failed: {e:#}");
                        let _ = tx
                            .send(Msg::Status {
                                target_id: *id,
                                status: app::TargetStatus::Error(e.to_string()),
                            })
                            .await;
                    }
                }
            }))
            .await;
        },
    ));
}

/// If the run task dies before its own `SyncFinished`, still end the run in the UI.
fn watch_sync_task<T: Send + 'static>(handle: tokio::task::JoinHandle<T>, tx: mpsc::Sender<Msg>) {
    tokio::spawn(async move {
        if let Err(e) = handle.await {
            log::warn!("[sync] tui run task panicked: {e}");
            let _ = tx
                .send(Msg::SyncFinished(action::failed_summary(format!(
                    "task panicked: {e}"
                ))))
                .await;
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicBool, Ordering};

    #[tokio::test]
    async fn offline_probe_reports_and_skips_the_work() {
        let (tx, mut rx) = mpsc::channel(4);
        let ran = Arc::new(AtomicBool::new(false));
        let flag = ran.clone();
        let r = with_online(|| Some(NoInternetReason::NoNetwork), tx, async move {
            flag.store(true, Ordering::SeqCst)
        })
        .await;
        assert_eq!(r, Err(NoInternetReason::NoNetwork));
        assert!(matches!(
            rx.recv().await,
            Some(Msg::Offline(Some(NoInternetReason::NoNetwork)))
        ));
        assert!(!ran.load(Ordering::SeqCst));
    }

    #[tokio::test]
    async fn online_probe_clears_banner_and_runs_the_work() {
        let (tx, mut rx) = mpsc::channel(4);
        let ran = Arc::new(AtomicBool::new(false));
        let flag = ran.clone();
        let r = with_online(
            || None,
            tx,
            async move { flag.store(true, Ordering::SeqCst) },
        )
        .await;
        assert_eq!(r, Ok(()));
        assert!(matches!(rx.recv().await, Some(Msg::Offline(None))));
        assert!(ran.load(Ordering::SeqCst));
    }
}
