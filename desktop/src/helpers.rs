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

/// Path-safe form of a group_key for the backups/ directory layout.
/// Single source of truth for writer (snapshot_save_dir) and readers.
pub fn sanitize_group_key(group_key: &str) -> String {
    // Replace `..` before slashes so `../x` → `_/x` → `__x` (2 underscores, not 3).
    group_key.replace("..", "_").replace(['/', '\\'], "_")
}

/// Recursively copy every file under `src_root` into `dst_root`, creating parents.
/// Overwrite-merge: does not delete files already in dst that are not in src.
pub fn copy_tree(src_root: &Path, dst_root: &Path) -> Result<()> {
    for file_path in walkdir(src_root)? {
        let rel = file_path.strip_prefix(src_root)?;
        let dest = dst_root.join(rel);
        if let Some(parent) = dest.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::copy(&file_path, &dest)?;
    }
    Ok(())
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

    let sanitized_key = sanitize_group_key(group_key);
    let ts = Utc::now().format("%Y%m%dT%H%M%S%.3fZ");
    let snap_dir = backups_root.join(&sanitized_key).join(ts.to_string());
    std::fs::create_dir_all(&snap_dir)?;

    copy_tree(src, &snap_dir)?;

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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotEntry {
    pub timestamp: String,
    pub path: PathBuf,
    pub file_count: usize,
    pub total_bytes: u64,
}

pub fn list_snapshots_in(backups_root: &Path, group_key: &str) -> Result<Vec<SnapshotEntry>> {
    let dir = backups_root.join(sanitize_group_key(group_key));
    if !dir.is_dir() {
        return Ok(Vec::new());
    }
    let mut entries = Vec::new();
    for e in std::fs::read_dir(&dir)? {
        let p = e?.path();
        if !p.is_dir() {
            continue;
        }
        let ts = p
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("")
            .to_string();
        if ts.is_empty() {
            continue;
        }
        let files = walkdir(&p)?;
        let total_bytes = files
            .iter()
            .filter_map(|f| std::fs::metadata(f).ok())
            .map(|m| m.len())
            .sum();
        entries.push(SnapshotEntry {
            timestamp: ts,
            path: p,
            file_count: files.len(),
            total_bytes,
        });
    }
    entries.sort_by(|a, b| b.timestamp.cmp(&a.timestamp));
    Ok(entries)
}

pub fn list_snapshots(group_key: &str) -> Result<Vec<SnapshotEntry>> {
    let backups_root = crate::config::WaystoneConfig::config_dir()?.join("backups");
    list_snapshots_in(&backups_root, group_key)
}

pub fn human_size(bytes: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = 1024 * 1024;
    if bytes >= MB {
        format!("{:.1} MB", bytes as f64 / MB as f64)
    } else if bytes >= KB {
        format!("{:.1} KB", bytes as f64 / KB as f64)
    } else {
        format!("{} B", bytes)
    }
}

pub fn restore_from_snapshot_in(
    backups_root: &Path,
    group_key: &str,
    timestamp: &str,
    dest: &Path,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    let snap_dir = backups_root
        .join(sanitize_group_key(group_key))
        .join(timestamp);
    if !snap_dir.is_dir() {
        anyhow::bail!("snapshot '{}' not found for {}", timestamp, group_key);
    }
    let guard = if safety_backup {
        snapshot_save_dir(dest, backups_root, group_key)
            .with_context(|| format!("safety backup failed for {group_key}; restore aborted"))?
    } else {
        None
    };
    copy_tree(&snap_dir, dest)?;
    Ok(guard)
}

pub fn restore_from_snapshot(
    group_key: &str,
    timestamp: &str,
    dest: &Path,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    let backups_root = crate::config::WaystoneConfig::config_dir()?.join("backups");
    restore_from_snapshot_in(&backups_root, group_key, timestamp, dest, safety_backup)
}

/// Perform a guarded restore: safety-snapshot the destination, fetch the blob,
/// and restore it to the local save directory. Returns the snapshot path if a
/// backup was taken.
#[allow(clippy::too_many_arguments)]
pub fn guarded_restore(
    vault: &waystone_core::crypto::Vault,
    dav: &dyn waystone_sync::WebDav,
    save: &waystone_core::model::NormalizedSave,
    hash: &str,
    dest: &Path,
    adapter_name: &str,
    system_name: &str,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    let snap = safety_snapshot(dest, &save.group_key, safety_backup)?;
    let zip_bytes = waystone_sync::fetch_blob(vault, save, hash, dav)?;
    restore_save_from_blob(&zip_bytes, save, dest, adapter_name, system_name)?;
    Ok(snap)
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

    #[test]
    fn sanitize_group_key_replaces_slashes_and_dotdot() {
        assert_eq!(
            sanitize_group_key("switch/GAME_001/main"),
            "switch_GAME_001_main"
        );
        assert_eq!(sanitize_group_key("3ds\\GAME\\slot"), "3ds_GAME_slot");
        assert_eq!(
            sanitize_group_key("../../../etc/passwd"),
            "______etc_passwd"
        );
        assert_eq!(sanitize_group_key("plain_key"), "plain_key");
    }

    #[test]
    fn copy_tree_copies_nested_structure() {
        let src = tempfile::tempdir().unwrap();
        let sub = src.path().join("subdir");
        std::fs::create_dir_all(&sub).unwrap();
        std::fs::write(src.path().join("a.sav"), b"aaa").unwrap();
        std::fs::write(sub.join("b.dat"), b"bbb").unwrap();
        let dst = tempfile::tempdir().unwrap();
        copy_tree(src.path(), dst.path()).unwrap();
        assert_eq!(std::fs::read(dst.path().join("a.sav")).unwrap(), b"aaa");
        assert_eq!(
            std::fs::read(dst.path().join("subdir").join("b.dat")).unwrap(),
            b"bbb"
        );
    }

    #[test]
    fn copy_tree_overwrites_existing_files() {
        let src = tempfile::tempdir().unwrap();
        std::fs::write(src.path().join("save.dat"), b"new-data").unwrap();
        let dst = tempfile::tempdir().unwrap();
        std::fs::write(dst.path().join("save.dat"), b"old-data").unwrap();
        std::fs::write(dst.path().join("extra.txt"), b"keep-me").unwrap();
        copy_tree(src.path(), dst.path()).unwrap();
        assert_eq!(
            std::fs::read(dst.path().join("save.dat")).unwrap(),
            b"new-data"
        );
        assert_eq!(
            std::fs::read(dst.path().join("extra.txt")).unwrap(),
            b"keep-me"
        );
    }

    #[test]
    fn list_snapshots_in_returns_entries_newest_first() {
        let backups = tempfile::tempdir().unwrap();
        let key_dir = backups.path().join("switch_GAME_001_main");
        let ts1 = key_dir.join("20260901T120000.000Z");
        let ts2 = key_dir.join("20260907T143100.000Z");
        std::fs::create_dir_all(&ts1).unwrap();
        std::fs::write(ts1.join("save.dat"), b"aaa").unwrap();
        std::fs::create_dir_all(&ts2).unwrap();
        std::fs::write(ts2.join("save.dat"), b"bbb").unwrap();
        std::fs::write(ts2.join("extra.sav"), b"cc").unwrap();
        let entries = list_snapshots_in(backups.path(), "switch/GAME_001/main").unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].timestamp, "20260907T143100.000Z");
        assert_eq!(entries[0].file_count, 2);
        assert_eq!(entries[0].total_bytes, 5);
        assert_eq!(entries[1].timestamp, "20260901T120000.000Z");
        assert_eq!(entries[1].file_count, 1);
        assert_eq!(entries[1].total_bytes, 3);
    }

    #[test]
    fn list_snapshots_in_returns_empty_for_missing_dir() {
        let backups = tempfile::tempdir().unwrap();
        let entries = list_snapshots_in(backups.path(), "nonexistent/key").unwrap();
        assert!(entries.is_empty());
    }

    #[test]
    fn list_snapshots_in_returns_empty_for_empty_dir() {
        let backups = tempfile::tempdir().unwrap();
        let key_dir = backups.path().join("switch_GAME_001_main");
        std::fs::create_dir_all(&key_dir).unwrap();
        let entries = list_snapshots_in(backups.path(), "switch/GAME_001/main").unwrap();
        assert!(entries.is_empty());
    }

    #[test]
    fn restore_from_snapshot_in_copies_files_to_dest() {
        let backups = tempfile::tempdir().unwrap();
        let snap_dir = backups
            .path()
            .join("switch_GAME_001_main")
            .join("20260907T143100.000Z");
        std::fs::create_dir_all(&snap_dir).unwrap();
        std::fs::write(snap_dir.join("save.dat"), b"snapshot-data").unwrap();
        let sub = snap_dir.join("subdir");
        std::fs::create_dir_all(&sub).unwrap();
        std::fs::write(sub.join("extra.sav"), b"extra").unwrap();
        let dest = tempfile::tempdir().unwrap();
        std::fs::write(dest.path().join("old.txt"), b"old").unwrap();
        let result = restore_from_snapshot_in(
            backups.path(),
            "switch/GAME_001/main",
            "20260907T143100.000Z",
            dest.path(),
            true,
        )
        .unwrap();
        assert!(result.is_some());
        assert_eq!(
            std::fs::read(dest.path().join("save.dat")).unwrap(),
            b"snapshot-data"
        );
        assert_eq!(
            std::fs::read(dest.path().join("subdir").join("extra.sav")).unwrap(),
            b"extra"
        );
        assert_eq!(std::fs::read(dest.path().join("old.txt")).unwrap(), b"old");
    }

    #[test]
    fn restore_from_snapshot_in_skips_guard_when_safety_off() {
        let backups = tempfile::tempdir().unwrap();
        let snap_dir = backups
            .path()
            .join("switch_GAME_001_main")
            .join("20260907T143100.000Z");
        std::fs::create_dir_all(&snap_dir).unwrap();
        std::fs::write(snap_dir.join("save.dat"), b"snapshot-data").unwrap();
        let dest = tempfile::tempdir().unwrap();
        std::fs::write(dest.path().join("existing.dat"), b"old").unwrap();
        let result = restore_from_snapshot_in(
            backups.path(),
            "switch/GAME_001/main",
            "20260907T143100.000Z",
            dest.path(),
            false,
        )
        .unwrap();
        assert!(result.is_none());
        assert_eq!(
            std::fs::read(dest.path().join("save.dat")).unwrap(),
            b"snapshot-data"
        );
    }

    #[test]
    fn restore_from_snapshot_in_errors_on_missing_timestamp() {
        let backups = tempfile::tempdir().unwrap();
        let key_dir = backups.path().join("switch_GAME_001_main");
        std::fs::create_dir_all(&key_dir).unwrap();
        let dest = tempfile::tempdir().unwrap();
        let result = restore_from_snapshot_in(
            backups.path(),
            "switch/GAME_001/main",
            "20260101T000000.000Z",
            dest.path(),
            true,
        );
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("not found"));
    }
}
