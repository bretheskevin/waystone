mod cli_session;
mod config;
mod helpers;
mod keychain;
mod logging;
mod net_status;
mod pipeline;
mod presets;
mod save_io;
mod sync_run;
mod tui;
mod updater;
mod vault_ops;
mod webdav;

use anyhow::Result;
use clap::{Parser, Subcommand};
use config::SyncTarget;
use std::path::PathBuf;
use waystone_core::model::NormalizedSave;
use waystone_sync::engine::{
    self, PlanAction, PlanEntry, SyncPhase, SyncProgressSink, SyncSummary,
};
use zeroize::Zeroize;

pub const APP_VERSION: &str = env!("WAYSTONE_VERSION");

#[derive(Parser)]
#[command(name = "waystone", version = APP_VERSION, about = "Game-save sync tool")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Initialize a new vault (passphrase + recovery key)
    Init {
        /// WebDAV server URL
        #[arg(long)]
        server: String,
        /// WebDAV username (stored in config; password from WAYSTONE_WEBDAV_PASSWORD or prompt)
        #[arg(long)]
        username: Option<String>,
    },
    /// Push local saves to the server
    Push {
        /// Path to the save source directory (e.g., JKSV folder)
        #[arg(long)]
        source: PathBuf,
        /// Adapter to use: jksv | mgba
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// WebDAV username override
        #[arg(long)]
        username: Option<String>,
    },
    /// Pull saves from the server
    Pull {
        /// Path to write restored saves
        #[arg(long)]
        dest: PathBuf,
        /// Adapter: jksv | mgba
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// WebDAV username override
        #[arg(long)]
        username: Option<String>,
    },
    /// Sync every configured target: push local changes, pull newer remote saves
    Sync {
        /// Print the plan without changing anything
        #[arg(long)]
        dry_run: bool,
        /// Only this target (by name)
        #[arg(long)]
        target: Option<String>,
    },
    /// Check for a newer release and install it
    Update {
        /// Only report whether an update is available
        #[arg(long)]
        check: bool,
    },
    /// Forget the keychain session (the next command asks for the passphrase again)
    Logout,
    /// Launch the interactive TUI dashboard
    Tui,
    /// Show sync status
    Status {
        /// Path to the save source directory
        #[arg(long)]
        source: PathBuf,
        /// Adapter: jksv | mgba
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// WebDAV username override
        #[arg(long)]
        username: Option<String>,
    },
    /// Browse and restore save history from the server
    History {
        #[command(subcommand)]
        action: HistoryCommands,
    },
    /// Browse and restore local safety-backup snapshots (offline)
    Snapshots {
        #[command(subcommand)]
        action: SnapshotCommands,
    },
}

#[derive(Subcommand)]
enum HistoryCommands {
    /// List history entries for saves under a source path
    List {
        /// Path to the save source directory
        #[arg(long)]
        source: PathBuf,
        /// Adapter: jksv | mgba | rom_keyed (alias: twilight; path = ROM root, system ignored) | checkpoint
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | nds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// WebDAV username override
        #[arg(long)]
        username: Option<String>,
    },
    /// Restore a specific history version to the local save directory
    Restore {
        /// Path to write restored saves
        #[arg(long)]
        dest: PathBuf,
        /// Adapter: jksv | mgba | rom_keyed (alias: twilight; path = ROM root, system ignored) | checkpoint
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | nds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// WebDAV username override
        #[arg(long)]
        username: Option<String>,
        /// Restrict to a specific game key (required when multiple saves exist)
        #[arg(long)]
        game: Option<String>,
        /// Version selector: timestamp, timestamp prefix, or hash prefix
        selector: String,
    },
}

#[derive(Subcommand)]
enum SnapshotCommands {
    /// List local safety-backup snapshots
    List {
        /// Path to the save source directory
        #[arg(long)]
        source: PathBuf,
        /// Adapter: jksv | mgba | rom_keyed (alias: twilight; path = ROM root, system ignored) | checkpoint
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | nds | gba | gbc | gb
        #[arg(long)]
        system: String,
    },
    /// Restore a local snapshot to the save directory
    Restore {
        /// Path to write restored saves
        #[arg(long)]
        dest: PathBuf,
        /// Adapter: jksv | mgba | rom_keyed (alias: twilight; path = ROM root, system ignored) | checkpoint
        #[arg(long)]
        adapter: String,
        /// System: switch | 3ds | nds | gba | gbc | gb
        #[arg(long)]
        system: String,
        /// Restrict to a specific game key (required when multiple saves exist)
        #[arg(long)]
        game: Option<String>,
        /// Timestamp selector (exact or prefix)
        selector: String,
    },
}

fn resolve_history_selector<'a>(
    entries: &'a [waystone_sync::HistoryEntry],
    selector: &str,
) -> Result<&'a waystone_sync::HistoryEntry> {
    let matches: Vec<&waystone_sync::HistoryEntry> = entries
        .iter()
        .filter(|e| {
            e.timestamp == selector
                || e.timestamp.starts_with(selector)
                || e.hash.starts_with(selector)
        })
        .collect();

    match matches.len() {
        0 => anyhow::bail!("no history entry matches selector '{}'", selector),
        1 => Ok(matches[0]),
        n => anyhow::bail!(
            "ambiguous selector '{}': matches {} entries; be more specific",
            selector,
            n
        ),
    }
}

fn resolve_snapshot_selector<'a>(
    entries: &'a [helpers::SnapshotEntry],
    selector: &str,
) -> Result<&'a helpers::SnapshotEntry> {
    let matches: Vec<&helpers::SnapshotEntry> = entries
        .iter()
        .filter(|e| e.timestamp == selector || e.timestamp.starts_with(selector))
        .collect();
    match matches.len() {
        0 => anyhow::bail!("no snapshot matches selector '{}'", selector),
        1 => Ok(matches[0]),
        n => anyhow::bail!(
            "ambiguous selector '{}': matches {} snapshots; be more specific",
            selector,
            n
        ),
    }
}

/// Pick the single save from `saves`, or the one whose game key matches `game`.
/// Errors when `game` is required but absent, or when the key is not found.
fn resolve_save_by_game<'a>(
    saves: &'a [waystone_core::model::NormalizedSave],
    game: Option<&str>,
) -> Result<&'a waystone_core::model::NormalizedSave> {
    if saves.is_empty() {
        anyhow::bail!("no saves found; nothing to resolve");
    } else if saves.len() == 1 {
        Ok(&saves[0])
    } else if let Some(game_key) = game {
        saves
            .iter()
            .find(|s| s.id.game.key == game_key)
            .ok_or_else(|| {
                let keys: Vec<&str> = saves.iter().map(|s| s.id.game.key.as_str()).collect();
                anyhow::anyhow!(
                    "game key '{}' not found; available: {}",
                    game_key,
                    keys.join(", ")
                )
            })
    } else {
        let keys: Vec<&str> = saves.iter().map(|s| s.id.game.key.as_str()).collect();
        anyhow::bail!(
            "multiple saves found; use --game to select one: {}",
            keys.join(", ")
        )
    }
}

pub fn format_plan_line(e: &PlanEntry, target_name: &str) -> String {
    let action = match e.action {
        PlanAction::Push => "push",
        PlanAction::Pull => "pull",
        PlanAction::InSync => "in-sync",
        PlanAction::Conflict => "conflict",
        PlanAction::Error => "error",
    };
    let suffix = e
        .error
        .as_deref()
        .map_or(String::new(), |err| format!(" \u{2014} {err}"));
    format!(
        "{action:<8} {target_name} \u{b7} {} / {}{suffix}",
        e.save.id.game.display_name, e.save.id.slot
    )
}

pub fn progress_line(index: usize, total: usize, name: &str, phase: SyncPhase) -> String {
    format!(
        "[{}/{total}] {} {name}",
        index + 1,
        sync_run::phase_label(phase)
    )
}

struct CliProgress;

impl SyncProgressSink for CliProgress {
    fn started(&self, total: usize) {
        println!("{total} item(s) to process");
    }

    fn item(&self, index: usize, total: usize, save: &NormalizedSave, phase: SyncPhase) {
        println!(
            "{}",
            progress_line(index, total, &save.id.game.display_name, phase)
        );
    }

    fn is_cancelled(&self) -> bool {
        false
    }
}

fn print_summary(summary: &SyncSummary) {
    println!("{}", summary.headline());
    for m in &summary.error_messages {
        println!("  {m}");
    }
}

/// A CLI path argument as a target, inheriting the preset/save dir of a matching configured target.
fn cli_target(
    cfg: &config::WaystoneConfig,
    path: &std::path::Path,
    adapter: &str,
    system: &str,
) -> SyncTarget {
    cfg.resolve_target(SyncTarget::plain("cli", path, adapter, system))
}

/// Push/pull one ad-hoc CLI path.
async fn run_adhoc(
    path: PathBuf,
    adapter: String,
    system: String,
    username: Option<String>,
    mode: sync_run::RunMode,
) -> Result<()> {
    let cfg = config::WaystoneConfig::load()?;
    let target = cli_target(&cfg, &path, &adapter, &system);
    run_planned(
        cfg,
        vec![target],
        sync_run::Selection::All,
        mode,
        username,
        false,
    )
    .await
}

async fn run_sync(dry_run: bool, target_name: Option<String>) -> Result<()> {
    let mut cfg = config::WaystoneConfig::load()?;
    if cfg.targets.is_empty() {
        anyhow::bail!("no targets configured -- add one in `waystone tui` first");
    }
    let sel = match target_name {
        None => sync_run::Selection::All,
        Some(name) => sync_run::Selection::Target(
            cfg.targets
                .iter()
                .position(|t| t.name == name)
                .ok_or_else(|| anyhow::anyhow!("no target named '{name}'"))?,
        ),
    };
    let targets = std::mem::take(&mut cfg.targets);
    run_planned(cfg, targets, sel, sync_run::RunMode::Sync, None, dry_run).await
}

/// Plan + (unless `dry_run`) execute through the shared engine; exits 1 when any item failed.
/// Remote-only ROM slots are discovered for sync and pull (`sync_run::wants_remote_only`), as in the TUI.
async fn run_planned(
    cfg: config::WaystoneConfig,
    targets: Vec<SyncTarget>,
    sel: sync_run::Selection,
    mode: sync_run::RunMode,
    username: Option<String>,
    dry_run: bool,
) -> Result<()> {
    let session = cli_session::open(&cfg, username, &keychain::OsKeychain).await?;
    let backups_root = helpers::default_backups_root()?;
    log::info!("[sync] cli run start: {sel:?} {mode:?} dry_run={dry_run}");
    let summary = tokio::task::spawn_blocking(move || -> Result<Option<SyncSummary>> {
        let dav: &dyn waystone_sync::WebDav = session.blocking.as_ref();
        let remote = sync_run::wants_remote_only(mode).then_some((session.vault.as_ref(), dav));
        let collected = sync_run::collect_saves(&targets, &sel, remote)?;
        let planned = sync_run::plan_run(
            &session.vault,
            collected,
            &cfg.device_id,
            cfg.conflict_policy,
            mode,
            dav,
        );
        for (e, owner) in planned.entries.iter().zip(&planned.owners) {
            println!("{}", format_plan_line(e, &targets[*owner].name));
        }
        for err in &planned.scan_errors {
            println!("error    {err}");
        }
        if dry_run {
            println!("{}", engine::describe(&planned.entries));
            return Ok(None);
        }
        Ok(Some(sync_run::execute_run(
            &session.vault,
            &planned,
            &targets,
            &cfg.device_id,
            cfg.safety_backup,
            backups_root,
            dav,
            &CliProgress,
        )))
    })
    .await??;
    if let Some(summary) = summary {
        log::info!("[sync] cli run done: {}", summary.headline());
        print_summary(&summary);
        if summary.errors > 0 {
            std::process::exit(1);
        }
    }
    Ok(())
}

async fn run_update(check_only: bool) -> Result<()> {
    let latest = tokio::task::spawn_blocking(|| updater::check(APP_VERSION)).await?;
    let rel = match latest {
        Err(e) => {
            eprintln!("{}", e.check_message());
            std::process::exit(1);
        }
        Ok(updater::CheckResult::UpToDate(v)) => {
            println!("Up to date (v{v})");
            return Ok(());
        }
        Ok(updater::CheckResult::Available(rel)) => rel,
    };
    println!("Update available: v{}", rel.version);
    if check_only {
        return Ok(());
    }
    let version = rel.version.clone();
    let result = tokio::task::spawn_blocking(move || {
        updater::install(&rel, |done, total| {
            if let Some(total) = total.filter(|t| *t > 0) {
                print!("\rDownloading... {}%", done * 100 / total);
                let _ = std::io::Write::flush(&mut std::io::stdout());
            }
        })
    })
    .await?;
    println!();
    match result {
        Ok(()) => println!("Updated to v{version} \u{2014} restart waystone to use it."),
        Err(e) => {
            eprintln!("{}", e.install_message());
            std::process::exit(1);
        }
    }
    Ok(())
}

#[tokio::main]
async fn main() {
    if let Err(e) = run().await {
        if let Some(off) = e.downcast_ref::<cli_session::Offline>() {
            log::warn!("[net] offline: {off}");
            eprintln!("{off}");
            std::process::exit(2);
        }
        log::warn!("[app] command failed: {e:#}");
        eprintln!("Error: {e:#}");
        std::process::exit(1);
    }
}

async fn run() -> Result<()> {
    let cli = Cli::parse();
    match config::WaystoneConfig::config_dir().and_then(|d| logging::init(&d)) {
        Ok(p) => log::info!(
            "[app] waystone-desktop v{APP_VERSION} ({}) start; log={}",
            env!("WAYSTONE_TARGET"),
            p.display()
        ),
        Err(e) => eprintln!("warning: file logging disabled: {e:#}"),
    }

    match cli.command {
        Commands::Init { server, username } => {
            let mut passphrase = rpassword::prompt_password("Enter passphrase: ")?;
            let mut confirm = rpassword::prompt_password("Confirm passphrase: ")?;
            let matches = passphrase == confirm;
            confirm.zeroize();
            if !matches {
                passphrase.zeroize();
                anyhow::bail!("passphrases do not match");
            }
            cli_session::ensure_online(&server).await?;

            let mut cfg = config::WaystoneConfig::load()?;
            cfg.server_url = server.clone();
            cfg.username = username.clone();
            let password = cli_session::resolve_webdav_password(
                username.as_deref(),
                None,
                &cli_session::prompt_secret,
            )?;
            let dav = webdav::WebDavClient::new(&server, username.clone(), password.clone());
            let result = vault_ops::init_vault(&dav, &passphrase).await;
            passphrase.zeroize();
            let (vault, recovery_key) = match result {
                Ok(v) => v,
                Err(e) if e.is::<vault_ops::VaultExists>() => {
                    cfg.save()?;
                    eprintln!("{}", vault_ops::VAULT_EXISTS);
                    eprintln!("Run any command (e.g. `waystone sync --dry-run`) to unlock it.");
                    std::process::exit(1);
                }
                Err(e) => return Err(e),
            };

            cfg.save()?;
            cli_session::remember(&keychain::OsKeychain, &server, username, &vault, password);

            println!("Vault initialized.");
            println!("Device ID: {}", cfg.device_id);
            println!();
            println!("RECOVERY KEY (save this somewhere safe, it cannot be shown again):");
            println!("  {}", recovery_key);

            Ok(())
        }

        Commands::Push {
            source,
            adapter,
            system,
            username,
        } => run_adhoc(source, adapter, system, username, sync_run::RunMode::Push).await,

        Commands::Pull {
            dest,
            adapter,
            system,
            username,
        } => run_adhoc(dest, adapter, system, username, sync_run::RunMode::Pull).await,

        Commands::Sync { dry_run, target } => run_sync(dry_run, target).await,

        Commands::Update { check } => run_update(check).await,

        Commands::Logout => {
            keychain::clear_session(&keychain::OsKeychain)?;
            println!("Logged out (keychain session cleared).");
            Ok(())
        }

        Commands::Tui => {
            let cfg = config::WaystoneConfig::load()?;
            let config_path = config::WaystoneConfig::config_dir()?.join("config.json");
            tui::run(cfg, config_path).await
        }

        Commands::Status {
            source,
            adapter,
            system,
            username: _,
        } => {
            let cfg = config::WaystoneConfig::load()?;
            let device_id = cfg.device_id.clone();
            let server_url = cfg.server_url.clone();
            let target = cli_target(&cfg, &source, &adapter, &system);

            tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                let saves = helpers::load_target_saves(&target)?;

                println!("Device: {}", device_id);
                println!("Server: {}", server_url);
                println!("Found {} local save(s):", saves.len());
                for save in &saves {
                    let (entry, _) = waystone_core::packaging::package(save);
                    println!(
                        "  {} / {} [{}] hash={}...",
                        save.id.game.display_name,
                        save.id.slot,
                        match save.id.kind {
                            waystone_core::model::SaveKind::Battery => "battery",
                            waystone_core::model::SaveKind::SaveState => "savestate",
                            waystone_core::model::SaveKind::Native => "native",
                        },
                        &entry.content.hash[..12],
                    );
                }
                Ok(())
            })
            .await??;
            Ok(())
        }

        Commands::History { action } => {
            let cfg = config::WaystoneConfig::load()?;
            match action {
                HistoryCommands::List {
                    source,
                    adapter,
                    system,
                    username,
                } => {
                    let target = cli_target(&cfg, &source, &adapter, &system);
                    let cli_session::CliSession {
                        vault: vault_arc,
                        blocking: blocking_dav,
                    } = cli_session::open(&cfg, username, &keychain::OsKeychain).await?;

                    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                        let saves = helpers::load_target_saves(&target)?;

                        for save in &saves {
                            println!("{} / {}:", save.id.game.display_name, save.id.slot);
                            let entries = waystone_sync::list_history(
                                &vault_arc,
                                save,
                                blocking_dav.as_ref(),
                            )?;
                            if entries.is_empty() {
                                println!("  (no history yet)");
                            } else {
                                for e in &entries {
                                    println!(
                                        "  {}  {}  {}  mtime={}",
                                        e.timestamp,
                                        e.device_id,
                                        &e.hash[..12.min(e.hash.len())],
                                        e.mtime
                                    );
                                }
                            }
                            println!();
                        }
                        Ok(())
                    })
                    .await??;
                    Ok(())
                }
                HistoryCommands::Restore {
                    dest,
                    adapter,
                    system,
                    username,
                    game,
                    selector,
                } => {
                    let target = cli_target(&cfg, &dest, &adapter, &system);
                    let cli_session::CliSession {
                        vault: vault_arc,
                        blocking: blocking_dav,
                    } = cli_session::open(&cfg, username, &keychain::OsKeychain).await?;
                    let safety_backup = cfg.safety_backup;

                    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                        let saves = helpers::load_target_saves(&target)?;

                        let save = resolve_save_by_game(&saves, game.as_deref())?;

                        let entries =
                            waystone_sync::list_history(&vault_arc, save, blocking_dav.as_ref())?;
                        let entry = resolve_history_selector(&entries, &selector)?;

                        println!(
                            "Restoring: {} / {} <- version {} ({})",
                            save.id.game.display_name,
                            save.id.slot,
                            entry.timestamp,
                            &entry.hash[..12.min(entry.hash.len())]
                        );

                        save_io::restore_one(&target, &save.group_key, safety_backup, |s, io| {
                            engine::restore_hash(
                                &vault_arc,
                                0,
                                s,
                                &entry.hash,
                                blocking_dav.as_ref(),
                                io,
                            )
                        })?;
                        println!("Restore complete.");
                        Ok(())
                    })
                    .await??;
                    Ok(())
                }
            }
        }
        Commands::Snapshots { action } => {
            let cfg = config::WaystoneConfig::load()?;
            match action {
                SnapshotCommands::List {
                    source,
                    adapter,
                    system,
                } => {
                    let target = cli_target(&cfg, &source, &adapter, &system);
                    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                        let saves = helpers::load_target_saves(&target)?;
                        for save in &saves {
                            println!("{} / {}:", save.id.game.display_name, save.id.slot);
                            let entries = helpers::list_snapshots(&save.group_key)?;
                            if entries.is_empty() {
                                println!("  (no snapshots)");
                            } else {
                                for e in &entries {
                                    println!(
                                        "  {}  {} files  {}",
                                        e.timestamp,
                                        e.file_count,
                                        helpers::human_size(e.total_bytes)
                                    );
                                }
                            }
                            println!();
                        }
                        Ok(())
                    })
                    .await??;
                    Ok(())
                }
                SnapshotCommands::Restore {
                    dest,
                    adapter,
                    system,
                    game,
                    selector,
                } => {
                    let safety_backup = cfg.safety_backup;
                    let target = cli_target(&cfg, &dest, &adapter, &system);
                    tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                        let saves = helpers::load_target_saves(&target)?;
                        if saves.is_empty() {
                            anyhow::bail!(
                                "no saves found at destination '{}'; nothing to restore",
                                dest.display()
                            );
                        }
                        let save = resolve_save_by_game(&saves, game.as_deref())?;
                        let entries = helpers::list_snapshots(&save.group_key)?;
                        let entry = resolve_snapshot_selector(&entries, &selector)?;
                        println!(
                            "Restoring: {} / {} <- snapshot {}",
                            save.id.game.display_name, save.id.slot, entry.timestamp
                        );
                        let restore = helpers::restore_target_for(&target, &save.group_key)?;
                        if let Some(p) = helpers::restore_from_snapshot(
                            &save.group_key,
                            &entry.timestamp,
                            &restore,
                            safety_backup,
                        )? {
                            println!("  safety backup -> {}", p.display());
                        }
                        println!("Restore complete.");
                        Ok(())
                    })
                    .await??;
                    Ok(())
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_sync::HistoryEntry;

    fn make_entries() -> Vec<HistoryEntry> {
        vec![
            HistoryEntry {
                timestamp: "20260907T143100Z".into(),
                device_id: "dev1".into(),
                hash: "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890".into(),
                mtime: "2026-09-07T14:31:00Z".into(),
            },
            HistoryEntry {
                timestamp: "20260906T120000Z".into(),
                device_id: "dev2".into(),
                hash: "1111111122222222333333334444444455555555666666667777777788888888".into(),
                mtime: "2026-09-06T12:00:00Z".into(),
            },
        ]
    }

    #[test]
    fn resolve_history_selector_exact_timestamp() {
        let entries = make_entries();
        let result = resolve_history_selector(&entries, "20260907T143100Z").unwrap();
        assert_eq!(result.timestamp, "20260907T143100Z");
    }

    #[test]
    fn resolve_history_selector_timestamp_prefix() {
        let entries = make_entries();
        let result = resolve_history_selector(&entries, "20260907").unwrap();
        assert_eq!(result.timestamp, "20260907T143100Z");
    }

    #[test]
    fn resolve_history_selector_hash_prefix() {
        let entries = make_entries();
        let result = resolve_history_selector(&entries, "abcdef").unwrap();
        assert_eq!(result.device_id, "dev1");
    }

    #[test]
    fn resolve_history_selector_no_match() {
        let entries = make_entries();
        let result = resolve_history_selector(&entries, "zzzzzzz");
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("no history entry matches"), "got: {err}");
    }

    #[test]
    fn resolve_history_selector_ambiguous() {
        let entries = make_entries();
        let result = resolve_history_selector(&entries, "2026090");
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("ambiguous"), "got: {err}");
    }

    // --- snapshot selector tests ---

    use helpers::SnapshotEntry;

    fn make_snapshot_entries() -> Vec<SnapshotEntry> {
        vec![
            SnapshotEntry {
                timestamp: "20260907T143100.000Z".into(),
                path: PathBuf::from("/backups/k/20260907T143100.000Z"),
                file_count: 3,
                total_bytes: 1024,
            },
            SnapshotEntry {
                timestamp: "20260906T120000.000Z".into(),
                path: PathBuf::from("/backups/k/20260906T120000.000Z"),
                file_count: 2,
                total_bytes: 512,
            },
        ]
    }

    #[test]
    fn resolve_snapshot_selector_exact_timestamp() {
        let entries = make_snapshot_entries();
        let result = resolve_snapshot_selector(&entries, "20260907T143100.000Z").unwrap();
        assert_eq!(result.timestamp, "20260907T143100.000Z");
    }

    #[test]
    fn resolve_snapshot_selector_timestamp_prefix() {
        let entries = make_snapshot_entries();
        let result = resolve_snapshot_selector(&entries, "20260907").unwrap();
        assert_eq!(result.timestamp, "20260907T143100.000Z");
    }

    #[test]
    fn resolve_snapshot_selector_no_match() {
        let entries = make_snapshot_entries();
        let result = resolve_snapshot_selector(&entries, "20250101");
        assert!(result.is_err());
        assert!(
            result
                .unwrap_err()
                .to_string()
                .contains("no snapshot matches")
        );
    }

    #[test]
    fn resolve_snapshot_selector_ambiguous() {
        let entries = make_snapshot_entries();
        let result = resolve_snapshot_selector(&entries, "2026090");
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("ambiguous"));
    }

    // --- resolve_save_by_game tests ---

    use waystone_core::model::{Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId};

    fn make_save(key: &str) -> NormalizedSave {
        NormalizedSave {
            id: SaveId {
                source: "test".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: key.to_string(),
                    display_name: key.to_string(),
                    confidence: Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: format!("switch/{}/main", key),
            portable: false,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![],
        }
    }

    #[test]
    fn resolve_save_by_game_single_save_no_game_arg() {
        let saves = vec![make_save("zelda")];
        let result = resolve_save_by_game(&saves, None).unwrap();
        assert_eq!(result.id.game.key, "zelda");
    }

    #[test]
    fn resolve_save_by_game_multi_save_matching_key() {
        let saves = vec![make_save("zelda"), make_save("mario")];
        let result = resolve_save_by_game(&saves, Some("mario")).unwrap();
        assert_eq!(result.id.game.key, "mario");
    }

    #[test]
    fn resolve_save_by_game_multi_save_missing_key() {
        let saves = vec![make_save("zelda"), make_save("mario")];
        let err = resolve_save_by_game(&saves, Some("sonic"))
            .unwrap_err()
            .to_string();
        assert!(err.contains("not found"), "got: {err}");
        assert!(err.contains("zelda"), "got: {err}");
    }

    #[test]
    fn resolve_save_by_game_multi_save_no_game_arg() {
        let saves = vec![make_save("zelda"), make_save("mario")];
        let err = resolve_save_by_game(&saves, None).unwrap_err().to_string();
        assert!(err.contains("use --game"), "got: {err}");
    }

    #[test]
    fn resolve_save_by_game_empty_saves() {
        let saves: Vec<NormalizedSave> = vec![];
        let err = resolve_save_by_game(&saves, None).unwrap_err().to_string();
        assert!(err.contains("no saves found"), "got: {err}");
    }

    #[test]
    fn app_version_matches_root_version_file() {
        assert_eq!(APP_VERSION, include_str!("../../VERSION").trim());
    }

    fn plan_entry(action: PlanAction, error: Option<&str>) -> PlanEntry {
        let save =
            waystone_core::adapters::mgba::MgbaAdapter::new(waystone_core::model::SystemId::Gba);
        use waystone_core::adapters::Adapter;
        let save = save
            .normalize(&waystone_core::model::RawTree {
                files: vec![waystone_core::model::RawFile {
                    path: "Zelda.sav".into(),
                    content: b"x".to_vec(),
                }],
            })
            .remove(0);
        let mut e = engine::forced_push(save).unwrap();
        e.action = action;
        e.error = error.map(String::from);
        e
    }

    #[test]
    fn plan_line_format() {
        let e = plan_entry(PlanAction::Push, None);
        assert_eq!(
            format_plan_line(&e, "GBA"),
            "push     GBA \u{b7} Zelda / battery"
        );
        let e = plan_entry(PlanAction::Error, Some("boom"));
        assert_eq!(
            format_plan_line(&e, "GBA"),
            "error    GBA \u{b7} Zelda / battery \u{2014} boom"
        );
    }

    #[test]
    fn progress_line_format() {
        assert_eq!(
            progress_line(0, 3, "Zelda", SyncPhase::Pushing),
            "[1/3] Uploading Zelda"
        );
        assert_eq!(
            progress_line(2, 3, "Zelda", SyncPhase::Pulling),
            "[3/3] Downloading Zelda"
        );
        assert_eq!(
            progress_line(1, 3, "Zelda", SyncPhase::Restoring),
            "[2/3] Restoring Zelda"
        );
    }
}
