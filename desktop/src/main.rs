mod config;
mod helpers;
mod pipeline;
mod tui;
mod webdav;

use anyhow::Result;
use clap::{Parser, Subcommand};
use std::path::PathBuf;
use waystone_core::conflict::SyncDecision;

#[derive(Parser)]
#[command(name = "waystone", version, about = "Game-save sync tool")]
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
        /// Adapter: jksv | mgba | twilight | checkpoint
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
        /// Adapter: jksv | mgba | twilight | checkpoint
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
        /// Adapter: jksv | mgba | twilight | checkpoint
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
        /// Adapter: jksv | mgba | twilight | checkpoint
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

/// Resolve WebDAV credentials: username from arg > config; password from env > interactive prompt.
fn resolve_webdav_credentials(
    username_arg: Option<String>,
    cfg: &config::WaystoneConfig,
) -> Result<(Option<String>, Option<String>)> {
    let username = username_arg.or_else(|| cfg.username.clone());
    if let Some(ref u) = username {
        let password = std::env::var("WAYSTONE_WEBDAV_PASSWORD")
            .ok()
            .or_else(|| rpassword::prompt_password(format!("WebDAV password for {u}: ")).ok());
        Ok((Some(u.clone()), password))
    } else {
        Ok((None, None))
    }
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

#[allow(clippy::too_many_arguments)]
fn do_pull_save(
    vault: &waystone_core::crypto::Vault,
    dav: &webdav::BlockingWebDav,
    save: &waystone_core::model::NormalizedSave,
    dest: &std::path::Path,
    adapter_name: &str,
    system_name: &str,
    device_id: &str,
    policy: waystone_core::conflict::ConflictPolicy,
    safety_backup: bool,
) -> Result<()> {
    let all_heads = waystone_sync::read_remote_heads(vault, save, dav)?;
    let (entry, _) = waystone_core::packaging::package(save);

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
            winner: waystone_core::conflict::ConflictWinner::Remote,
            ..
        } => waystone_core::conflict::fold_heads(&all_heads).map(|m| m.hash),
        _ => None,
    };

    match pull_hash {
        Some(hash) => {
            println!("Pulling: {} / {}", save.id.game.display_name, save.id.slot);
            if let Some(p) = helpers::guarded_restore(
                vault,
                dav,
                save,
                &hash,
                dest,
                adapter_name,
                system_name,
                safety_backup,
            )? {
                println!("  safety backup -> {}", p.display());
            }
        }
        None => match decision {
            SyncDecision::InSync => {
                println!("In sync: {} / {}", save.id.game.display_name, save.id.slot)
            }
            _ => println!(
                "Skipping (needs manual resolve): {} / {}",
                save.id.game.display_name, save.id.slot
            ),
        },
    }
    Ok(())
}

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { server, username } => {
            let passphrase = rpassword::prompt_password("Enter passphrase: ")?;
            let confirm = rpassword::prompt_password("Confirm passphrase: ")?;
            if passphrase != confirm {
                anyhow::bail!("passphrases do not match");
            }

            let (vault, recovery_key) = waystone_core::crypto::Vault::init(&passphrase)?;
            let keys_json = vault.keys_json()?;

            let cfg = config::WaystoneConfig {
                server_url: server.clone(),
                username: username.clone(),
                ..Default::default()
            };

            let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
            let dav = webdav::WebDavClient::new(&server, wdav_user, wdav_pass);
            dav.mkdir_p("/").await?;
            dav.put("/keys.json", keys_json).await?;

            cfg.save()?;

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
        } => {
            let cfg = config::WaystoneConfig::load()?;
            let system = helpers::parse_system(&system)?;
            let adapter = helpers::make_adapter(&adapter, system)?;

            let passphrase = rpassword::prompt_password("Passphrase: ")?;
            let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
            let dav =
                webdav::WebDavClient::new(&cfg.server_url, wdav_user.clone(), wdav_pass.clone());
            let keys_data = dav.get("/keys.json").await?.ok_or_else(|| {
                anyhow::anyhow!("no keys.json on server — run `waystone init` first")
            })?;
            let vault =
                waystone_core::crypto::Vault::unlock_with_passphrase(&passphrase, &keys_data)?;

            let raw = helpers::read_source_tree(&source)?;
            let saves = adapter.normalize(&raw);
            let count = saves.len();

            let vault_arc = std::sync::Arc::new(vault);
            let device_id = cfg.device_id.clone();
            let server_url_owned = cfg.server_url.clone();

            tokio::task::spawn_blocking(move || -> anyhow::Result<()> {
                let blocking_dav =
                    webdav::BlockingWebDav::new(&server_url_owned, wdav_user, wdav_pass);
                for save in &saves {
                    println!("Pushing: {} / {}", save.id.game.display_name, save.id.slot);
                    waystone_sync::push_one(&vault_arc, save, &device_id, &blocking_dav)?;
                }
                Ok(())
            })
            .await??;

            println!("Done. {} save(s) pushed.", count);
            Ok(())
        }

        Commands::Pull {
            dest,
            adapter,
            system,
            username,
        } => {
            let cfg = config::WaystoneConfig::load()?;

            let passphrase = rpassword::prompt_password("Passphrase: ")?;
            let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
            let dav =
                webdav::WebDavClient::new(&cfg.server_url, wdav_user.clone(), wdav_pass.clone());
            let keys_data = dav
                .get("/keys.json")
                .await?
                .ok_or_else(|| anyhow::anyhow!("no keys.json on server"))?;
            let vault =
                waystone_core::crypto::Vault::unlock_with_passphrase(&passphrase, &keys_data)?;

            let system_id = helpers::parse_system(&system)?;
            let adapter_obj = helpers::make_adapter(&adapter, system_id)?;

            let raw = if dest.exists() {
                helpers::read_source_tree(&dest)?
            } else {
                waystone_core::model::RawTree { files: vec![] }
            };
            let local_saves = adapter_obj.normalize(&raw);

            let server_url_owned = cfg.server_url.clone();
            let blocking_dav = tokio::task::spawn_blocking(move || {
                webdav::BlockingWebDav::new(&server_url_owned, wdav_user, wdav_pass)
            })
            .await?;
            let device_id = cfg.device_id.clone();
            let policy = cfg.conflict_policy;
            let safety_backup = cfg.safety_backup;

            for save in &local_saves {
                do_pull_save(
                    &vault,
                    &blocking_dav,
                    save,
                    &dest,
                    &adapter,
                    &system,
                    &device_id,
                    policy,
                    safety_backup,
                )?;
            }

            println!("Pull complete.");
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
            let system = helpers::parse_system(&system)?;
            let adapter = helpers::make_adapter(&adapter, system)?;
            let raw = helpers::read_source_tree(&source)?;
            let saves = adapter.normalize(&raw);

            println!("Device: {}", cfg.device_id);
            println!("Server: {}", cfg.server_url);
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
                    let passphrase = rpassword::prompt_password("Passphrase: ")?;
                    let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
                    let dav = webdav::WebDavClient::new(
                        &cfg.server_url,
                        wdav_user.clone(),
                        wdav_pass.clone(),
                    );
                    let keys_data = dav
                        .get("/keys.json")
                        .await?
                        .ok_or_else(|| anyhow::anyhow!("no keys.json on server"))?;
                    let vault = waystone_core::crypto::Vault::unlock_with_passphrase(
                        &passphrase,
                        &keys_data,
                    )?;

                    let system_id = helpers::parse_system(&system)?;
                    let adapter_obj = helpers::make_adapter(&adapter, system_id)?;
                    let raw = helpers::read_source_tree(&source)?;
                    let saves = adapter_obj.normalize(&raw);

                    let server_url_owned = cfg.server_url.clone();
                    let blocking_dav = tokio::task::spawn_blocking(move || {
                        webdav::BlockingWebDav::new(&server_url_owned, wdav_user, wdav_pass)
                    })
                    .await?;
                    for save in &saves {
                        println!("{} / {}:", save.id.game.display_name, save.id.slot);
                        let entries = waystone_sync::list_history(&vault, save, &blocking_dav)?;
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
                }
                HistoryCommands::Restore {
                    dest,
                    adapter,
                    system,
                    username,
                    game,
                    selector,
                } => {
                    let passphrase = rpassword::prompt_password("Passphrase: ")?;
                    let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
                    let dav = webdav::WebDavClient::new(
                        &cfg.server_url,
                        wdav_user.clone(),
                        wdav_pass.clone(),
                    );
                    let keys_data = dav
                        .get("/keys.json")
                        .await?
                        .ok_or_else(|| anyhow::anyhow!("no keys.json on server"))?;
                    let vault = waystone_core::crypto::Vault::unlock_with_passphrase(
                        &passphrase,
                        &keys_data,
                    )?;

                    let system_id = helpers::parse_system(&system)?;
                    let adapter_obj = helpers::make_adapter(&adapter, system_id)?;
                    let raw = if dest.exists() {
                        helpers::read_source_tree(&dest)?
                    } else {
                        waystone_core::model::RawTree { files: vec![] }
                    };
                    let saves = adapter_obj.normalize(&raw);

                    let save = if saves.len() == 1 {
                        &saves[0]
                    } else if let Some(ref game_key) = game {
                        saves
                            .iter()
                            .find(|s| s.id.game.key == *game_key)
                            .ok_or_else(|| {
                                let keys: Vec<&str> =
                                    saves.iter().map(|s| s.id.game.key.as_str()).collect();
                                anyhow::anyhow!(
                                    "game key '{}' not found; available: {}",
                                    game_key,
                                    keys.join(", ")
                                )
                            })?
                    } else {
                        let keys: Vec<&str> =
                            saves.iter().map(|s| s.id.game.key.as_str()).collect();
                        anyhow::bail!(
                            "multiple saves found; use --game to select one: {}",
                            keys.join(", ")
                        );
                    };

                    let server_url_owned = cfg.server_url.clone();
                    let blocking_dav = tokio::task::spawn_blocking(move || {
                        webdav::BlockingWebDav::new(&server_url_owned, wdav_user, wdav_pass)
                    })
                    .await?;
                    let entries = waystone_sync::list_history(&vault, save, &blocking_dav)?;
                    let entry = resolve_history_selector(&entries, &selector)?;

                    println!(
                        "Restoring: {} / {} <- version {} ({})",
                        save.id.game.display_name,
                        save.id.slot,
                        entry.timestamp,
                        &entry.hash[..12.min(entry.hash.len())]
                    );

                    if let Some(p) = helpers::guarded_restore(
                        &vault,
                        &blocking_dav,
                        save,
                        &entry.hash,
                        &dest,
                        &adapter,
                        &system,
                        cfg.safety_backup,
                    )? {
                        println!("  safety backup -> {}", p.display());
                    }
                    println!("Restore complete.");
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
                    let system_id = helpers::parse_system(&system)?;
                    let adapter_obj = helpers::make_adapter(&adapter, system_id)?;
                    let raw = helpers::read_source_tree(&source)?;
                    let saves = adapter_obj.normalize(&raw);
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
                }
                SnapshotCommands::Restore {
                    dest,
                    adapter,
                    system,
                    game,
                    selector,
                } => {
                    let system_id = helpers::parse_system(&system)?;
                    let adapter_obj = helpers::make_adapter(&adapter, system_id)?;
                    let raw = if dest.exists() {
                        helpers::read_source_tree(&dest)?
                    } else {
                        waystone_core::model::RawTree { files: vec![] }
                    };
                    let saves = adapter_obj.normalize(&raw);
                    if saves.is_empty() {
                        anyhow::bail!(
                            "no saves found at destination '{}'; nothing to restore",
                            dest.display()
                        );
                    }
                    let save = if saves.len() == 1 {
                        &saves[0]
                    } else if let Some(ref game_key) = game {
                        saves
                            .iter()
                            .find(|s| s.id.game.key == *game_key)
                            .ok_or_else(|| {
                                let keys: Vec<&str> =
                                    saves.iter().map(|s| s.id.game.key.as_str()).collect();
                                anyhow::anyhow!(
                                    "game key '{}' not found; available: {}",
                                    game_key,
                                    keys.join(", ")
                                )
                            })?
                    } else {
                        let keys: Vec<&str> =
                            saves.iter().map(|s| s.id.game.key.as_str()).collect();
                        anyhow::bail!(
                            "multiple saves found; use --game to select one: {}",
                            keys.join(", ")
                        );
                    };
                    let entries = helpers::list_snapshots(&save.group_key)?;
                    let entry = resolve_snapshot_selector(&entries, &selector)?;
                    println!(
                        "Restoring: {} / {} <- snapshot {}",
                        save.id.game.display_name, save.id.slot, entry.timestamp
                    );
                    if let Some(p) = helpers::restore_from_snapshot(
                        &save.group_key,
                        &entry.timestamp,
                        &dest,
                        cfg.safety_backup,
                    )? {
                        println!("  safety backup -> {}", p.display());
                    }
                    println!("Restore complete.");
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
}
