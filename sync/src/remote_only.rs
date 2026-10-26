use crate::WebDav;
use crate::orchestration::href_last_segment;
use std::collections::{BTreeMap, BTreeSet};
use waystone_core::crypto::Vault;
use waystone_core::model::{
    Confidence, GameRef, NormalizedSave, SaveId, SaveKind, SystemId, build_group_key,
};
use waystone_core::rom_systems::slot_names;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RomRef {
    pub system: SystemId,
    pub rom_id: String,
    pub display_name: String,
}

fn remote_only_save(rom: &RomRef, slot: &str) -> NormalizedSave {
    NormalizedSave {
        id: SaveId {
            source: "rom_keyed".into(),
            system: rom.system,
            game: GameRef {
                key: rom.rom_id.clone(),
                display_name: rom.display_name.clone(),
                confidence: Confidence::Strong,
                title_id: None,
                serial: None,
                rom_crc: None,
            },
            slot: slot.into(),
            kind: SaveKind::Battery,
        },
        group_key: build_group_key(rom.system, &rom.rom_id, slot),
        portable: true,
        mtime: String::new(),
        files: Vec::new(),
    }
}

/// ROM slots present on the server but not locally (3DS semantics); remote-only saves have no files.
pub fn remote_only_saves(
    vault: &Vault,
    roms: &[RomRef],
    local_group_keys: &BTreeSet<String>,
    dav: &dyn WebDav,
) -> Vec<NormalizedSave> {
    let mut by_system: BTreeMap<&str, (SystemId, Vec<&RomRef>)> = BTreeMap::new();
    for r in roms {
        by_system
            .entry(r.system.as_str())
            .or_insert((r.system, Vec::new()))
            .1
            .push(r);
    }
    let mut out = Vec::new();
    for (sys_name, (system, group)) in by_system {
        let slots = slot_names(system);
        let pending: Vec<&RomRef> = group
            .into_iter()
            .filter(|r| {
                slots
                    .iter()
                    .any(|s| !local_group_keys.contains(&build_group_key(system, &r.rom_id, s)))
            })
            .collect();
        if pending.is_empty() {
            continue;
        }
        let sys_seg = vault.path_segment(sys_name);
        let games: BTreeSet<String> = match dav.propfind(&sys_seg) {
            Ok(h) => h.iter().map(|x| href_last_segment(x).to_string()).collect(),
            Err(e) => {
                log::warn!("[roms] remote-only: PROPFIND {sys_name} failed: {e}");
                continue;
            }
        };
        log::info!(
            "[roms] remote set {sys_name}: {} entr(ies), {} ROM(s) to check",
            games.len(),
            pending.len()
        );
        for rom in pending {
            let game_seg = vault.path_segment(&rom.rom_id);
            if !games.contains(&game_seg) {
                continue;
            }
            let remote_slots: BTreeSet<String> =
                match dav.propfind(&format!("{sys_seg}/{game_seg}")) {
                    Ok(h) => h.iter().map(|x| href_last_segment(x).to_string()).collect(),
                    Err(e) => {
                        log::warn!(
                            "[roms] remote-only: PROPFIND {sys_name}/{} failed: {e}",
                            rom.rom_id
                        );
                        continue;
                    }
                };
            for slot in &slots {
                let gk = build_group_key(system, &rom.rom_id, slot);
                if local_group_keys.contains(&gk)
                    || !remote_slots.contains(&vault.path_segment(slot))
                {
                    continue;
                }
                log::info!("[roms] remote-only {gk}");
                out.push(remote_only_save(rom, slot));
            }
        }
    }
    log::info!("[roms] remote-only probe done: {} slot(s)", out.len());
    out
}
