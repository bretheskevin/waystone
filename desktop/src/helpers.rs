use anyhow::{Context, Result};
use chrono::Utc;
use std::path::{Path, PathBuf};
use waystone_core::adapters::Adapter;
use waystone_core::adapters::rom_keyed::{RomKeyedAdapter, rom_keyed_to_native};
use waystone_core::crc32::Crc32;
use waystone_core::model::{NormalizedSave, RawFile, RawTree, SystemId};
use waystone_core::rom_id::{ROM_HEADER_LEN, display_name, needs_full_hash, rom_identity};
use waystone_core::rom_pair::pair_roms;
use waystone_core::rom_systems::base_name;

pub fn is_rom_keyed(adapter: &str) -> bool {
    matches!(adapter, "rom_keyed" | "twilight")
}

pub fn parse_system(s: &str) -> Result<SystemId> {
    SystemId::parse(s).ok_or_else(|| anyhow::anyhow!("unknown system: {}", s))
}

pub fn make_adapter(name: &str, system: SystemId) -> Result<Box<dyn Adapter>> {
    match name {
        "jksv" => Ok(Box::new(waystone_core::adapters::jksv::JksvAdapter::new(
            system,
        ))),
        "mgba" => Ok(Box::new(waystone_core::adapters::mgba::MgbaAdapter::new(
            system,
        ))),
        name if is_rom_keyed(name) => {
            anyhow::bail!("[roms] '{name}' is per-ROM; use load_rom_keyed_saves / restore_target")
        }
        "checkpoint" => Ok(Box::new(
            waystone_core::adapters::checkpoint::CheckpointAdapter::new(system),
        )),
        other => anyhow::bail!("unknown adapter: {}", other),
    }
}

#[derive(Debug, Clone)]
pub struct LocalRom {
    pub system: SystemId,
    pub rom_file_name: String,
    pub rom_id: String,
    pub display_name: String,
    pub save_dir: PathBuf,
    pub save_paths: Vec<PathBuf>,
}

/// Header bytes + identity; streams a full-file CRC32 only when core asks for it.
pub fn rom_identity_of_file(system: SystemId, path: &Path) -> Result<(String, Vec<u8>)> {
    use std::io::{Read, Seek, SeekFrom};
    let mut f =
        std::fs::File::open(path).with_context(|| format!("[roms] open {}", path.display()))?;
    let mut header = Vec::with_capacity(ROM_HEADER_LEN);
    (&mut f)
        .take(ROM_HEADER_LEN as u64)
        .read_to_end(&mut header)
        .with_context(|| format!("[roms] read header {}", path.display()))?;
    let crc = if needs_full_hash(system, &header) {
        f.seek(SeekFrom::Start(0))?;
        let mut c = Crc32::new();
        let mut buf = vec![0u8; 64 * 1024];
        loop {
            let n = f
                .read(&mut buf)
                .with_context(|| format!("[roms] hash {}", path.display()))?;
            if n == 0 {
                break;
            }
            c.update(&buf[..n]);
        }
        Some(c.finish())
    } else {
        None
    };
    let id = rom_identity(system, &header, crc)
        .with_context(|| format!("[roms] no identity for {}", path.display()))?;
    Ok((id, header))
}

pub fn scan_local_roms(root: &Path) -> Result<Vec<LocalRom>> {
    scan_local_roms_where(root, |_| true)
}

fn scan_local_roms_where(root: &Path, keep: impl Fn(SystemId) -> bool) -> Result<Vec<LocalRom>> {
    let rels: Vec<String> = walkdir(root)
        .with_context(|| format!("[roms] walk {}", root.display()))?
        .iter()
        .filter_map(|p| p.strip_prefix(root).ok())
        .map(|r| r.to_string_lossy().replace('\\', "/"))
        .collect();
    let mut roms = Vec::new();
    for pairing in pair_roms(&rels).into_iter().filter(|p| keep(p.system)) {
        let rom_file_name = base_name(&pairing.rom_path).to_string();
        let (rom_id, header) = rom_identity_of_file(pairing.system, &root.join(&pairing.rom_path))?;
        roms.push(LocalRom {
            system: pairing.system,
            display_name: display_name(pairing.system, &header, &rom_file_name),
            rom_file_name,
            rom_id,
            save_dir: root.join(&pairing.save_dir),
            save_paths: pairing.save_paths.iter().map(|s| root.join(s)).collect(),
        });
    }
    Ok(roms)
}

pub fn load_rom_keyed_saves(root: &Path) -> Result<Vec<NormalizedSave>> {
    let mut saves = Vec::new();
    for rom in scan_local_roms(root)? {
        if rom.save_paths.is_empty() {
            continue;
        }
        let mut files = Vec::new();
        for p in &rom.save_paths {
            let name = p
                .file_name()
                .and_then(|n| n.to_str())
                .with_context(|| format!("[roms] non-UTF-8 save name {}", p.display()))?;
            let content =
                std::fs::read(p).with_context(|| format!("[roms] read save {}", p.display()))?;
            files.push(RawFile {
                path: name.to_string(),
                content,
            });
        }
        let adapter =
            RomKeyedAdapter::new(rom.system, rom.rom_id, rom.display_name, rom.rom_file_name);
        saves.extend(adapter.normalize(&RawTree { files }));
    }
    Ok(saves)
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

/// Load and normalize saves from `source` using the given adapter and system.
///
/// The `Box<dyn Adapter>` is created and dropped inside this function so the
/// returned `Vec<NormalizedSave>` is `Send`. A missing or empty `source`
/// directory normalizes to zero saves, not an error.
pub fn load_saves(
    adapter_name: &str,
    system_name: &str,
    source: &Path,
) -> Result<Vec<NormalizedSave>> {
    if is_rom_keyed(adapter_name) {
        return load_rom_keyed_saves(source);
    }
    let system = parse_system(system_name)?;
    let adapter = make_adapter(adapter_name, system)?;
    let raw = read_source_tree(source)?;
    Ok(adapter.normalize(&raw))
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

    let snap_dir = new_snapshot_dir(backups_root, group_key);
    std::fs::create_dir_all(&snap_dir)?;

    copy_tree(src, &snap_dir)?;

    Ok(Some(snap_dir))
}

#[derive(Debug, Clone)]
pub struct RestoreTarget {
    pub dir: PathBuf,
    pub guard_files: Option<Vec<PathBuf>>,
    pub rom_file_name: Option<String>,
}

impl RestoreTarget {
    pub fn dir(path: &Path) -> Self {
        Self {
            dir: path.to_path_buf(),
            guard_files: None,
            rom_file_name: None,
        }
    }
}

/// Where a restore writes: the target root for folder adapters, the matching ROM's save dir for rom_keyed.
pub fn restore_target(adapter: &str, dest: &Path, group_key: &str) -> Result<RestoreTarget> {
    if !is_rom_keyed(adapter) {
        return Ok(RestoreTarget::dir(dest));
    }
    let mut parts = group_key.splitn(3, '/');
    let (Some(sys), Some(rom_id), Some(_)) = (parts.next(), parts.next(), parts.next()) else {
        anyhow::bail!("[roms] malformed group key {group_key}");
    };
    let system = parse_system(sys)?;
    let rom = scan_local_roms_where(dest, |s| s == system)?
        .into_iter()
        .find(|r| r.rom_id == rom_id)
        .with_context(|| {
            format!(
                "[roms] no local ROM for {group_key} under {}; cannot restore",
                dest.display()
            )
        })?;
    Ok(RestoreTarget {
        dir: rom.save_dir,
        guard_files: Some(rom.save_paths),
        rom_file_name: Some(rom.rom_file_name),
    })
}

fn new_snapshot_dir(backups_root: &Path, group_key: &str) -> PathBuf {
    let ts = Utc::now().format("%Y%m%dT%H%M%S%.3fZ");
    backups_root
        .join(sanitize_group_key(group_key))
        .join(ts.to_string())
}

pub fn snapshot_files(
    files: &[PathBuf],
    backups_root: &Path,
    group_key: &str,
) -> Result<Option<PathBuf>> {
    if files.is_empty() {
        return Ok(None);
    }
    let snap_dir = new_snapshot_dir(backups_root, group_key);
    std::fs::create_dir_all(&snap_dir)?;
    for f in files {
        let name = f
            .file_name()
            .with_context(|| format!("[roms] save path without file name {}", f.display()))?;
        std::fs::copy(f, snap_dir.join(name))
            .with_context(|| format!("[roms] snapshot copy {}", f.display()))?;
    }
    Ok(Some(snap_dir))
}

pub fn snapshot_target(
    target: &RestoreTarget,
    backups_root: &Path,
    group_key: &str,
) -> Result<Option<PathBuf>> {
    match &target.guard_files {
        Some(files) => snapshot_files(files, backups_root, group_key),
        None => snapshot_save_dir(&target.dir, backups_root, group_key),
    }
}

/// Run the safety-backup snapshot before a destructive restore.
/// Returns the snapshot path, `None` if src was empty/missing or backup is disabled.
pub fn safety_snapshot(
    target: &RestoreTarget,
    group_key: &str,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    if !safety_backup {
        return Ok(None);
    }
    let backups_root = crate::config::WaystoneConfig::config_dir()?.join("backups");
    snapshot_target(target, &backups_root, group_key)
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

/// Reformat a compact UTC timestamp ("YYYYMMDDTHHMMSS[.fff]Z") as
/// "YYYY-MM-DD HH:MM" for display. Render-only: the compact form is kept for
/// storage and lexicographic sorting. Unknown shapes are returned unchanged.
pub fn human_timestamp(ts: &str) -> String {
    let mut d = ts.strip_suffix('Z').unwrap_or(ts);
    if let Some((head, _)) = d.split_once('.') {
        d = head;
    }
    let b = d.as_bytes();
    // Reject non-ASCII before slicing at fixed byte offsets — a stray multibyte
    // char (e.g. garbage filename from a backup dir listing) would panic on a
    // non-char-boundary slice.
    if b.len() < 15 || b[8] != b'T' || !b[..15].iter().all(|c| c.is_ascii()) {
        return ts.to_string();
    }
    format!(
        "{}-{}-{} {}:{}",
        &d[0..4],
        &d[4..6],
        &d[6..8],
        &d[9..11],
        &d[11..13]
    )
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
    target: &RestoreTarget,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    let snap_dir = backups_root
        .join(sanitize_group_key(group_key))
        .join(timestamp);
    if !snap_dir.is_dir() {
        anyhow::bail!("snapshot '{}' not found for {}", timestamp, group_key);
    }
    let guard = if safety_backup {
        snapshot_target(target, backups_root, group_key)
            .with_context(|| format!("safety backup failed for {group_key}; restore aborted"))?
    } else {
        None
    };
    copy_tree(&snap_dir, &target.dir)?;
    Ok(guard)
}

pub fn restore_from_snapshot(
    group_key: &str,
    timestamp: &str,
    target: &RestoreTarget,
    safety_backup: bool,
) -> Result<Option<PathBuf>> {
    let backups_root = crate::config::WaystoneConfig::config_dir()?.join("backups");
    restore_from_snapshot_in(&backups_root, group_key, timestamp, target, safety_backup)
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
    let target = restore_target(adapter_name, dest, &save.group_key)?;
    let snap = safety_snapshot(&target, &save.group_key, safety_backup)?;
    let zip_bytes = waystone_sync::fetch_blob(vault, save, hash, dav)?;
    restore_save_from_blob(&zip_bytes, save, &target, adapter_name, system_name)?;
    Ok(snap)
}

/// Unzips `zip_bytes`, converts files to native adapter layout, and writes them under `dest`.
///
/// Creates parent directories as needed. The adapter is constructed internally so no
/// `!Send` adapter reference crosses async boundaries.
pub fn restore_save_from_blob(
    zip_bytes: &[u8],
    save: &waystone_core::model::NormalizedSave,
    target: &RestoreTarget,
    adapter_name: &str,
    system_name: &str,
) -> Result<()> {
    let files = waystone_core::packaging::unzip(zip_bytes)?;
    let native: Vec<(String, Vec<u8>)> = match &target.rom_file_name {
        Some(rom_file_name) => {
            let native = rom_keyed_to_native(save.id.system, &save.id.slot, rom_file_name, &files);
            if native.is_empty() {
                anyhow::bail!(
                    "[roms] blob for {} has no file for slot {}",
                    save.group_key,
                    save.id.slot
                );
            }
            native
        }
        None => {
            let adapter = make_adapter(adapter_name, parse_system(system_name)?)?;
            let mut restored = save.clone();
            restored.files = files;
            adapter
                .to_native(&restored)
                .files
                .into_iter()
                .map(|f| (f.path, f.content))
                .collect()
        }
    };
    for (rel, content) in &native {
        let path = target.dir.join(rel);
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        std::fs::write(&path, content)
            .with_context(|| format!("[roms] write {}", path.display()))?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::model::{Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId};

    #[test]
    fn human_timestamp_formats_compact_and_millis() {
        assert_eq!(human_timestamp("20260928T124245Z"), "2026-09-28 12:42");
        assert_eq!(human_timestamp("20260907T143100.000Z"), "2026-09-07 14:31");
    }

    #[test]
    fn human_timestamp_passes_through_unknown_shapes() {
        assert_eq!(human_timestamp("garbage"), "garbage");
        assert_eq!(human_timestamp(""), "");
    }

    #[test]
    fn human_timestamp_passes_through_non_ascii() {
        // A multibyte char inside the digit run must not panic on a
        // non-char-boundary slice — return the raw value unchanged.
        let ts = "20260928T12\u{00E9}45Z";
        assert_eq!(human_timestamp(ts), ts);
    }

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
    fn parse_system_accepts_new_rom_systems() {
        assert_eq!(parse_system("snes").unwrap(), SystemId::Snes);
        assert!(
            parse_system("bogus")
                .unwrap_err()
                .to_string()
                .contains("unknown system")
        );
        assert!(is_rom_keyed("twilight") && is_rom_keyed("rom_keyed") && !is_rom_keyed("mgba"));
    }

    fn nds_rom(code: &[u8; 4], crc16: u16) -> Vec<u8> {
        let mut h = vec![0u8; 0x400];
        h[0x0C..0x10].copy_from_slice(code);
        h[0x15E..0x160].copy_from_slice(&crc16.to_le_bytes());
        h
    }

    fn write(p: &Path, bytes: &[u8]) {
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(p, bytes).unwrap();
    }

    #[test]
    fn rom_keyed_load_pairs_by_rom_identity() {
        let root = tempfile::tempdir().unwrap();
        write(
            &root.path().join("roms/nds/Mario.nds"),
            &nds_rom(b"AMCE", 0x1A2B),
        );
        write(&root.path().join("roms/nds/saves/Mario.sav"), b"nds-save");
        write(&root.path().join("roms/gb/Tetris.gb"), b"123456789");
        write(&root.path().join("roms/gb/Tetris.sav"), b"gb-save");
        write(&root.path().join("roms/gb/NoSave.gb"), b"abc");
        let mut saves = load_saves("twilight", "", root.path()).unwrap();
        saves.sort_by(|a, b| a.group_key.cmp(&b.group_key));
        let keys: Vec<_> = saves.iter().map(|s| s.group_key.as_str()).collect();
        assert_eq!(keys, vec!["gb/CBF43926/battery", "nds/AMCE-1A2B/battery"]);
        assert_eq!(
            saves[1].files,
            vec![("battery".to_string(), b"nds-save".to_vec())]
        );
        assert_eq!(saves[0].id.game.display_name, "Tetris");
    }

    #[test]
    fn rom_keyed_load_on_missing_root_is_empty() {
        assert!(
            load_saves("rom_keyed", "", Path::new("/nonexistent_rom_root_42"))
                .unwrap()
                .is_empty()
        );
    }

    #[test]
    fn rom_keyed_restore_writes_under_local_rom_name() {
        let root = tempfile::tempdir().unwrap();
        write(
            &root.path().join("roms/nds/Mario Kart (E).nds"),
            &nds_rom(b"AMCE", 0x1A2B),
        );
        let remote = RomKeyedAdapter::new(SystemId::Nds, "AMCE-1A2B", "MKDS", "mkds.nds")
            .normalize(&RawTree {
                files: vec![RawFile {
                    path: "mkds.sav".into(),
                    content: b"remote".to_vec(),
                }],
            });
        let (_, zip) = waystone_core::packaging::package(&remote[0]);
        let target = restore_target("rom_keyed", root.path(), &remote[0].group_key).unwrap();
        assert!(target.guard_files.as_ref().unwrap().is_empty());
        restore_save_from_blob(&zip, &remote[0], &target, "rom_keyed", "").unwrap();
        assert_eq!(
            std::fs::read(root.path().join("roms/nds/saves/Mario Kart (E).sav")).unwrap(),
            b"remote"
        );
    }

    #[test]
    fn rom_keyed_restore_without_local_rom_errors() {
        let root = tempfile::tempdir().unwrap();
        let err = restore_target("rom_keyed", root.path(), "nds/AMCE-1A2B/battery").unwrap_err();
        assert!(err.to_string().contains("no local ROM"), "got: {err}");
    }

    #[test]
    fn rom_keyed_safety_snapshot_only_copies_that_roms_saves() {
        let root = tempfile::tempdir().unwrap();
        write(&root.path().join("roms/gb/A.gb"), b"aaaa");
        write(&root.path().join("roms/gb/B.gb"), b"bbbb");
        write(&root.path().join("roms/gb/saves/A.sav"), b"a-save");
        write(&root.path().join("roms/gb/saves/B.sav"), b"b-save");
        let key = format!("gb/{:08X}/battery", waystone_core::crc32::crc32(b"aaaa"));
        let target = restore_target("rom_keyed", root.path(), &key).unwrap();
        let backups = tempfile::tempdir().unwrap();
        let snap = snapshot_target(&target, backups.path(), &key)
            .unwrap()
            .unwrap();
        let names: Vec<_> = std::fs::read_dir(&snap)
            .unwrap()
            .map(|e| e.unwrap().file_name())
            .collect();
        assert_eq!(names, vec![std::ffi::OsString::from("A.sav")]);
    }

    #[test]
    fn load_saves_on_missing_path_returns_empty() {
        let saves = load_saves(
            "jksv",
            "switch",
            Path::new("/nonexistent_xyz_load_saves_99"),
        )
        .unwrap();
        assert!(saves.is_empty());
    }

    #[test]
    fn load_saves_unknown_adapter_returns_error() {
        let tmp = tempfile::tempdir().unwrap();
        let err = load_saves("bogus_adapter", "switch", tmp.path()).unwrap_err();
        assert!(err.to_string().contains("unknown adapter"), "got: {err}");
    }

    #[test]
    fn load_saves_unknown_system_returns_error() {
        let tmp = tempfile::tempdir().unwrap();
        let err = load_saves("jksv", "bogus_system", tmp.path()).unwrap_err();
        assert!(err.to_string().contains("unknown system"), "got: {err}");
    }

    #[test]
    fn restore_save_from_blob_writes_files() {
        let save = make_test_save();
        let (_, zip_bytes) = waystone_core::packaging::package(&save);
        let dest = tempfile::tempdir().unwrap();

        restore_save_from_blob(
            &zip_bytes,
            &save,
            &RestoreTarget::dir(dest.path()),
            "jksv",
            "switch",
        )
        .unwrap();

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
            &RestoreTarget::dir(dest.path()),
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
            &RestoreTarget::dir(dest.path()),
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
            &RestoreTarget::dir(dest.path()),
            true,
        );
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("not found"));
    }
}
