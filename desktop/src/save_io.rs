use crate::config::SyncTarget;
use crate::helpers;
use std::path::{Path, PathBuf};
use waystone_core::model::NormalizedSave;
use waystone_sync::engine::{RestoreError, SaveIo};

pub struct DesktopSaveIo<'a> {
    pub targets: &'a [SyncTarget],
    /// plan index -> target index
    pub owners: &'a [usize],
    pub safety_backup: bool,
    pub backups_root: PathBuf,
}

impl SaveIo for DesktopSaveIo<'_> {
    fn restore(
        &self,
        index: usize,
        save: &NormalizedSave,
        files: Vec<(String, Vec<u8>)>,
    ) -> Result<bool, String> {
        let target = self
            .owners
            .get(index)
            .and_then(|t| self.targets.get(*t))
            .ok_or("no target for plan entry")?;
        guarded_write(target, save, &files, self.safety_backup, &self.backups_root)
            .map_err(|e| format!("{e:#}"))
    }
}

/// Snapshot first (a failed snapshot aborts with Ok(false)), then write; pruning is non-fatal.
pub fn guarded_write(
    target: &SyncTarget,
    save: &NormalizedSave,
    files: &[(String, Vec<u8>)],
    safety_backup: bool,
    backups_root: &Path,
) -> anyhow::Result<bool> {
    let rt = helpers::restore_target_for(target, &save.group_key)?;
    if let Err(e) = helpers::safety_snapshot(&rt, &save.group_key, safety_backup, backups_root) {
        log::warn!("[saves] {}: {e:#} -- aborting restore", save.group_key);
        return Ok(false);
    }
    helpers::write_save_files(files, save, &rt, &target.adapter, &target.system)?;
    Ok(true)
}

/// Runs `restore` (a history restore or a pull) against the one `save_key` of `target`,
/// through the same flavored write path as a full sync. Ok(false) from the engine is an error here.
pub fn restore_one(
    target: &SyncTarget,
    save_key: &str,
    safety_backup: bool,
    restore: impl FnOnce(&NormalizedSave, &DesktopSaveIo) -> Result<bool, RestoreError>,
) -> anyhow::Result<()> {
    let saves = helpers::load_target_saves(target)?;
    let save = saves
        .iter()
        .find(|s| s.group_key == save_key)
        .ok_or_else(|| anyhow::anyhow!("save '{save_key}' not found in target"))?;
    let io = DesktopSaveIo {
        targets: std::slice::from_ref(target),
        owners: &[0],
        safety_backup,
        backups_root: helpers::default_backups_root()?,
    };
    if restore(save, &io)? {
        Ok(())
    } else {
        anyhow::bail!("restore of {save_key} aborted")
    }
}
