pub mod action;
pub mod app;
pub mod event;
pub mod theme;
pub mod ui;

use crate::config::WaystoneConfig;
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
use std::io::stdout;
use std::path::PathBuf;
use tokio::sync::mpsc;

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

pub async fn run(config: WaystoneConfig, config_path: PathBuf) -> Result<()> {
    let _guard = TerminalGuard;
    let mut terminal = TerminalGuard::init()?;

    let original_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |info| {
        let _ = disable_raw_mode();
        let _ = stdout().execute(LeaveAlternateScreen);
        original_hook(info);
    }));

    let mut app = App::new(&config);
    let (tx, mut rx) = mpsc::channel::<Msg>(64);
    let mut event_stream = EventStream::new();

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
                dispatch_cmd(&mut app, cmd, &config, &config_path, tx.clone()).await?;
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
    config: &WaystoneConfig,
    config_path: &std::path::Path,
    tx: mpsc::Sender<Msg>,
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

        Cmd::SpawnPush(target_id) => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                app.busy = Some(target_id);
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let cfg = config.clone();
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    let creds = app::SessionCreds { vault, dav };
                    if let Err(e) =
                        action::push_target(&target, &creds, &cfg, tx_action.clone(), target_id)
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

        Cmd::SpawnPull(target_id) => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                app.busy = Some(target_id);
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let cfg = config.clone();
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    let creds = app::SessionCreds { vault, dav };
                    if let Err(e) =
                        action::pull_target(&target, &creds, &cfg, tx_action.clone(), target_id)
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

        Cmd::RefreshStatus(target_id) => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let cfg = config.clone();
                let tx = tx.clone();
                tokio::spawn(async move {
                    let creds = app::SessionCreds { vault, dav };
                    if let Err(e) =
                        action::refresh_status(&target, &creds, &cfg, tx.clone(), target_id).await
                    {
                        let _ = tx
                            .send(Msg::Status {
                                target_id,
                                status: app::TargetStatus::Error(e.to_string()),
                            })
                            .await;
                    }
                });
            }
        }

        Cmd::RefreshAllStatuses => {
            if let Some(creds) = &app.creds {
                for target_id in 0..app.targets.len() {
                    if let Some(target) = app.targets.get(target_id).cloned() {
                        let vault = creds.vault.clone();
                        let dav = creds.dav.clone();
                        let cfg = config.clone();
                        let tx = tx.clone();
                        tokio::spawn(async move {
                            let creds = app::SessionCreds { vault, dav };
                            if let Err(e) =
                                action::refresh_status(&target, &creds, &cfg, tx.clone(), target_id)
                                    .await
                            {
                                let _ = tx
                                    .send(Msg::Status {
                                        target_id,
                                        status: app::TargetStatus::Error(e.to_string()),
                                    })
                                    .await;
                            }
                        });
                    }
                }
            }
        }

        Cmd::AttemptUnlock {
            passphrase,
            webdav_password,
        } => {
            let server_url = app.server_url.clone();
            let username = app.username.clone();
            let tx = tx.clone();
            tokio::spawn(async move {
                action::attempt_unlock(server_url, username, passphrase, webdav_password, tx).await;
            });
        }

        Cmd::ResolveKeepLocal {
            target_id,
            save_key,
        } => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                app.busy = Some(target_id);
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let cfg = config.clone();
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    let creds = app::SessionCreds { vault, dav };
                    if let Err(e) = action::resolve_keep_local(
                        &target,
                        &save_key,
                        &creds,
                        &cfg,
                        tx_action.clone(),
                        target_id,
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

        Cmd::ResolveKeepRemote {
            target_id,
            save_key,
            remote_hash,
        } => {
            if let (Some(creds), Some(target)) = (&app.creds, app.targets.get(target_id).cloned()) {
                app.busy = Some(target_id);
                let vault = creds.vault.clone();
                let dav = creds.dav.clone();
                let cfg = config.clone();
                let tx_action = tx.clone();
                let handle = tokio::spawn(async move {
                    let creds = app::SessionCreds { vault, dav };
                    if let Err(e) = action::resolve_keep_remote(
                        &target,
                        &save_key,
                        &remote_hash,
                        &creds,
                        &cfg,
                        tx_action.clone(),
                        target_id,
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
