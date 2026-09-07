use anyhow::{Context, Result};
use chrono::Utc;
use std::path::{Path, PathBuf};
use waystone_core::adapters::Adapter;
use waystone_core::model::{RawFile, RawTree, SystemId};

pub fn parse_system(s: &str) -> Result<SystemId> {
    match s {
        "switch" => Ok(SystemId::Switch),
        "3ds" => Ok(SystemId::ThreeDS),
        "nds" => Ok(SystemId::Nds),
        "gba" => Ok(SystemId::Gba),
        "gbc" => Ok(SystemId::Gbc),
        "gb" => Ok(SystemId::Gb),
        other => anyhow::bail!("unknown system: {}", other),
    }
}

pub fn make_adapter(name: &str, system: SystemId) -> Result<Box<dyn Adapter>> {
    match name {
        "jksv" => Ok(Box::new(waystone_core::adapters::jksv::JksvAdapter::new(
            system,
        ))),
        "mgba" => Ok(Box::new(waystone_core::adapters::mgba::MgbaAdapter::new(
            system,
        ))),
        "twilight" => Ok(Box::new(
            waystone_core::adapters::twilight::TwilightAdapter::new(),
        )),
        "checkpoint" => Ok(Box::new(
            waystone_core::adapters::checkpoint::CheckpointAdapter::new(system),
        )),
        other => anyhow::bail!("unknown adapter: {}", other),
    }
}

pub fn read_source_tree(path: &Path) -> Result<RawTree> {
    let mut files = Vec::new();
    for entry in walkdir(path)? {
        let rel = entry.strip_prefix(path)?.to_string_lossy().to_string();
        let content = std::fs::read(&entry)?;
        files.push(RawFile { path: rel, content });
    }
    Ok(RawTree { files })
}

pub fn walkdir(dir: &Path) -> Result<Vec<PathBuf>> {
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

/// Recursively copy the current local save tree to a timestamped backup dir.
/// Returns the snapshot path on success. `Ok(None)` when `src` is missing or
/// empty (nothing to back up). `Err` on any fs failure (caller aborts the restore).
pub fn snapshot_save_dir(
    src: &Path,
    backups_root: &Path,
    group_key: &str,
) -> Result<Option<PathBuf>> {
    if !src.is_dir() {
        return Ok(None);
    }
    let files = walkdir(src)?;
    if files.is_empty() {
        return Ok(None);
    }

    let sanitized_key = group_key.replace(['/', '\\'], "_").replace("..", "__");
    let ts = Utc::now().format("%Y%m%dT%H%M%S%.3fZ");
    let snap_dir = backups_root.join(&sanitized_key).join(ts.to_string());
    std::fs::create_dir_all(&snap_dir)?;

    for file_path in &files {
        let rel = file_path.strip_prefix(src)?;
        let dest = snap_dir.join(rel);
        if let Some(parent) = dest.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::copy(file_path, &dest)?;
    }

    Ok(Some(snap_dir))
}

/// Run the safety-backup snapshot before a destructive restore.
/// Returns the snapshot path, `None` if src was empty/missing or backup is disabled.
pub fn safety_snapshot(
    dest: &Path,
    group_key: &str,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    if !safety_backup {
        return Ok(None);
    }
    let backups_root = crate::config::WaystoneConfig::config_dir()?.join("backups");
    snapshot_save_dir(dest, &backups_root, group_key)
        .with_context(|| format!("safety backup failed for {group_key}; restore aborted"))
}

/// Unzips `zip_bytes`, converts files to native adapter layout, and writes them under `dest`.
///
/// Creates parent directories as needed. The adapter is constructed internally so no
/// `!Send` adapter reference crosses async boundaries.
pub fn restore_save_from_blob(
    zip_bytes: &[u8],
    save: &waystone_core::model::NormalizedSave,
    dest: &Path,
    adapter_name: &str,
    system_name: &str,
) -> Result<()> {
    let files = waystone_core::packaging::unzip(zip_bytes)?;
    let system = parse_system(system_name)?;
    let adapter = make_adapter(adapter_name, system)?;
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
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::model::{Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId};

    fn make_test_save() -> NormalizedSave {
        NormalizedSave {
            id: SaveId {
                source: "jksv".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: "TEST_GAME".into(),
                    display_name: "Test Game".into(),
                    confidence: Confidence::Strong,
                    title_id: Some("TEST_GAME".into()),
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: "switch/TEST_GAME/main".into(),
            portable: true,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![("save.dat".into(), b"original-content".to_vec())],
        }
    }

    #[test]
    fn restore_save_from_blob_writes_files() {
        let save = make_test_save();
        let (_, zip_bytes) = waystone_core::packaging::package(&save);
        let dest = tempfile::tempdir().unwrap();

        restore_save_from_blob(&zip_bytes, &save, dest.path(), "jksv", "switch").unwrap();

        // JKSV adapter writes: <Title> - <titleID>/<slot>/<filename>
        // So the file ends up at: dest/Test Game - TEST_GAME/main/save.dat
        // (or similar native layout). Walk the tree and check a .dat file exists.
        let written: Vec<_> = walkdir(dest.path()).unwrap();
        assert!(
            !written.is_empty(),
            "expected at least one file to be written"
        );
        let any_dat = written
            .iter()
            .any(|p| p.extension().is_some_and(|e| e == "dat"));
        assert!(any_dat, "expected a .dat file in restored tree");
    }

    #[test]
    fn snapshot_save_dir_copies_nested_tree() {
        let src = tempfile::tempdir().unwrap();
        let sub = src.path().join("subdir");
        std::fs::create_dir_all(&sub).unwrap();
        std::fs::write(src.path().join("a.sav"), b"aaa").unwrap();
        std::fs::write(sub.join("b.dat"), b"bbb").unwrap();

        let backups = tempfile::tempdir().unwrap();
        let result = snapshot_save_dir(src.path(), backups.path(), "switch/GAME_001/main").unwrap();

        let snap_path = result.expect("should return Some(path)");
        assert!(snap_path.exists());

        let a = std::fs::read(snap_path.join("a.sav")).unwrap();
        assert_eq!(a, b"aaa");
        let b = std::fs::read(snap_path.join("subdir").join("b.dat")).unwrap();
        assert_eq!(b, b"bbb");
    }

    #[test]
    fn snapshot_save_dir_returns_none_on_missing_src() {
        let backups = tempfile::tempdir().unwrap();
        let result = snapshot_save_dir(
            Path::new("/nonexistent_xyz_42"),
            backups.path(),
            "switch/G/s",
        )
        .unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn snapshot_save_dir_returns_none_on_empty_src() {
        let src = tempfile::tempdir().unwrap();
        let backups = tempfile::tempdir().unwrap();
        let result = snapshot_save_dir(src.path(), backups.path(), "switch/G/s").unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn snapshot_save_dir_returns_err_on_unwritable_dest() {
        let src = tempfile::tempdir().unwrap();
        std::fs::write(src.path().join("save.dat"), b"data").unwrap();

        let result = snapshot_save_dir(
            src.path(),
            Path::new("/nonexistent_root_xyz/backups"),
            "switch/G/s",
        );
        assert!(result.is_err());
    }
}
