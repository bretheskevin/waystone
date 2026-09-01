mod config;
mod pipeline;
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
}

fn parse_system(s: &str) -> Result<waystone_core::model::SystemId> {
    match s {
        "switch" => Ok(waystone_core::model::SystemId::Switch),
        "3ds" => Ok(waystone_core::model::SystemId::ThreeDS),
        "nds" => Ok(waystone_core::model::SystemId::Nds),
        "gba" => Ok(waystone_core::model::SystemId::Gba),
        "gbc" => Ok(waystone_core::model::SystemId::Gbc),
        "gb" => Ok(waystone_core::model::SystemId::Gb),
        other => anyhow::bail!("unknown system: {}", other),
    }
}

fn make_adapter(
    name: &str,
    system: waystone_core::model::SystemId,
) -> Result<Box<dyn waystone_core::adapters::Adapter>> {
    match name {
        "jksv" => Ok(Box::new(waystone_core::adapters::jksv::JksvAdapter::new(
            system,
        ))),
        "mgba" => Ok(Box::new(waystone_core::adapters::mgba::MgbaAdapter::new(
            system,
        ))),
        other => anyhow::bail!("unknown adapter: {}", other),
    }
}

fn read_source_tree(path: &std::path::Path) -> Result<waystone_core::model::RawTree> {
    let mut files = Vec::new();
    for entry in walkdir(path)? {
        let rel = entry.strip_prefix(path)?.to_string_lossy().to_string();
        let content = std::fs::read(&entry)?;
        files.push(waystone_core::model::RawFile { path: rel, content });
    }
    Ok(waystone_core::model::RawTree { files })
}

fn walkdir(dir: &std::path::Path) -> Result<Vec<PathBuf>> {
    let mut result = Vec::new();
    if dir.is_dir() {
        for entry in std::fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.is_dir() {
                result.extend(walkdir(&path)?);
            } else {
                result.push(path);
            }
        }
    }
    Ok(result)
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

async fn do_pull_save(
    pipe: &pipeline::SyncPipeline<'_>,
    save: &waystone_core::model::NormalizedSave,
    dest: &std::path::Path,
    adapter: &dyn waystone_core::adapters::Adapter,
    device_id: &str,
    policy: waystone_core::conflict::ConflictPolicy,
) -> Result<()> {
    let all_heads = pipe.read_all_remote_heads(save).await?;
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
            let zip_bytes = pipe.pull_blob(save, &hash).await?;
            let files = waystone_core::packaging::unzip(&zip_bytes)?;
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
            let system = parse_system(&system)?;
            let adapter = make_adapter(&adapter, system)?;

            let passphrase = rpassword::prompt_password("Passphrase: ")?;
            let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
            let dav = webdav::WebDavClient::new(&cfg.server_url, wdav_user, wdav_pass);
            let keys_data = dav.get("/keys.json").await?.ok_or_else(|| {
                anyhow::anyhow!("no keys.json on server — run `waystone init` first")
            })?;
            let vault =
                waystone_core::crypto::Vault::unlock_with_passphrase(&passphrase, &keys_data)?;

            let raw = read_source_tree(&source)?;
            let saves = adapter.normalize(&raw);

            let pipe = pipeline::SyncPipeline {
                vault: &vault,
                dav: &dav,
                device_id: &cfg.device_id,
                policy: cfg.conflict_policy,
            };

            for save in &saves {
                println!("Pushing: {} / {}", save.id.game.display_name, save.id.slot);
                pipe.push(save).await?;
            }

            println!("Done. {} save(s) pushed.", saves.len());
            Ok(())
        }

        Commands::Pull {
            dest,
            adapter,
            system,
            username,
        } => {
            let cfg = config::WaystoneConfig::load()?;
            let system = parse_system(&system)?;
            let adapter = make_adapter(&adapter, system)?;

            let passphrase = rpassword::prompt_password("Passphrase: ")?;
            let (wdav_user, wdav_pass) = resolve_webdav_credentials(username, &cfg)?;
            let dav = webdav::WebDavClient::new(&cfg.server_url, wdav_user, wdav_pass);
            let keys_data = dav
                .get("/keys.json")
                .await?
                .ok_or_else(|| anyhow::anyhow!("no keys.json on server"))?;
            let vault =
                waystone_core::crypto::Vault::unlock_with_passphrase(&passphrase, &keys_data)?;

            let raw = if dest.exists() {
                read_source_tree(&dest)?
            } else {
                waystone_core::model::RawTree { files: vec![] }
            };
            let local_saves = adapter.normalize(&raw);

            let pipe = pipeline::SyncPipeline {
                vault: &vault,
                dav: &dav,
                device_id: &cfg.device_id,
                policy: cfg.conflict_policy,
            };

            for save in &local_saves {
                do_pull_save(
                    &pipe,
                    save,
                    &dest,
                    adapter.as_ref(),
                    &cfg.device_id,
                    cfg.conflict_policy,
                )
                .await?;
            }

            println!("Pull complete.");
            Ok(())
        }

        Commands::Status {
            source,
            adapter,
            system,
            username: _,
        } => {
            let cfg = config::WaystoneConfig::load()?;
            let system = parse_system(&system)?;
            let adapter = make_adapter(&adapter, system)?;
            let raw = read_source_tree(&source)?;
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
    }
}
