use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::model::SystemId;
use crate::rom_systems::{
    GLOBAL_SAVE_DIR, base_name, classify_save, rom_system, rom_system_for_file,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RomPairing {
    pub system: SystemId,
    pub rom_path: String,
    pub save_dir: String,
    pub save_paths: Vec<String>,
}

fn parent_dir(path: &str) -> &str {
    path.rfind('/').map_or("", |i| &path[..i])
}

fn join(dir: &str, name: &str) -> String {
    if dir.is_empty() {
        name.to_string()
    } else {
        format!("{}/{}", dir, name)
    }
}

fn clean(path: &str) -> &str {
    let mut p = path;
    while let Some(rest) = p.strip_prefix("./") {
        p = rest;
    }
    p.trim_start_matches('/')
}

fn is_internal(path: &str) -> bool {
    let b = path.as_bytes();
    b.len() >= 5 && b[..5].eq_ignore_ascii_case(b"_nds/")
}

// TWiLight Menu++ hides dot entries (fileBrowse.cpp): AppleDouble `._*`, `.DS_Store`, `.Trashes/`.
fn is_hidden(path: &str) -> bool {
    path.split('/').any(|c| c.starts_with('.'))
}

pub fn save_dir_candidates(system: SystemId, rom_path: &str) -> Vec<String> {
    if let Some(fixed) = rom_system(system).and_then(|r| r.fixed_save_dir) {
        return vec![fixed.to_string()];
    }
    let dir = parent_dir(rom_path);
    vec![
        join(dir, "saves"),
        dir.to_string(),
        GLOBAL_SAVE_DIR.to_string(),
    ]
}

pub fn pair_roms(paths: &[String]) -> Vec<RomPairing> {
    let mut by_dir: BTreeMap<String, Vec<&str>> = BTreeMap::new();
    let mut roms: Vec<(SystemId, &str)> = Vec::new();
    for raw in paths {
        let p = clean(raw);
        if p.is_empty() || is_hidden(p) {
            continue;
        }
        by_dir
            .entry(parent_dir(p).to_ascii_lowercase())
            .or_default()
            .push(p);
        if !is_internal(p)
            && let Some(rs) = rom_system_for_file(p)
        {
            roms.push((rs.system, p));
        }
    }
    roms.sort_by(|a, b| a.1.cmp(b.1));
    roms.dedup_by(|a, b| a.1 == b.1);

    let mut found: Vec<Vec<Vec<String>>> = Vec::with_capacity(roms.len());
    let mut claims: BTreeMap<String, usize> = BTreeMap::new();
    for &(system, rom) in &roms {
        let rom_name = base_name(rom);
        let mut per_dir = Vec::new();
        for dir in save_dir_candidates(system, rom) {
            let mut hits: Vec<String> = by_dir
                .get(&dir.to_ascii_lowercase())
                .map(|files| {
                    files
                        .iter()
                        .filter(|f| {
                            **f != rom && classify_save(system, rom_name, base_name(f)).is_some()
                        })
                        .map(|f| f.to_string())
                        .collect()
                })
                .unwrap_or_default();
            hits.sort();
            hits.dedup();
            for h in &hits {
                *claims.entry(h.to_ascii_lowercase()).or_insert(0) += 1;
            }
            per_dir.push(hits);
        }
        found.push(per_dir);
    }

    roms.iter()
        .zip(found)
        .map(|(&(system, rom), per_dir)| {
            let chosen = per_dir
                .into_iter()
                .map(|hits| {
                    hits.into_iter()
                        .filter(|h| claims.get(&h.to_ascii_lowercase()) == Some(&1))
                        .collect::<Vec<_>>()
                })
                .find(|hits| !hits.is_empty());
            match chosen {
                Some(save_paths) => RomPairing {
                    system,
                    rom_path: rom.to_string(),
                    save_dir: parent_dir(&save_paths[0]).to_string(),
                    save_paths,
                },
                None => RomPairing {
                    system,
                    rom_path: rom.to_string(),
                    save_dir: save_dir_candidates(system, rom).swap_remove(0),
                    save_paths: Vec::new(),
                },
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rom_systems::NGP_SAVE_DIR;

    fn p(list: &[&str]) -> Vec<RomPairing> {
        pair_roms(&list.iter().map(|s| s.to_string()).collect::<Vec<_>>())
    }

    #[test]
    fn saves_subdir_mode() {
        let r = p(&[
            "roms/nds/Mario.nds",
            "roms/nds/saves/Mario.sav",
            "roms/nds/saves/Mario.sav1",
        ]);
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].system, SystemId::Nds);
        assert_eq!(r[0].save_dir, "roms/nds/saves");
        assert_eq!(
            r[0].save_paths,
            vec!["roms/nds/saves/Mario.sav", "roms/nds/saves/Mario.sav1"]
        );
    }

    #[test]
    fn next_to_rom_mode() {
        let r = p(&["roms/gba/Zelda.gba", "roms/gba/Zelda.sav"]);
        assert_eq!(r[0].save_dir, "roms/gba");
        assert_eq!(r[0].save_paths, vec!["roms/gba/Zelda.sav"]);
    }

    #[test]
    fn global_twilight_saves_mode() {
        let r = p(&[
            "roms/snes/Mario World.sfc",
            "_nds/TWiLightMenu/saves/Mario World.srm",
        ]);
        assert_eq!(r[0].save_dir, GLOBAL_SAVE_DIR);
        assert_eq!(
            r[0].save_paths,
            vec!["_nds/TWiLightMenu/saves/Mario World.srm"]
        );
    }

    #[test]
    fn saves_dir_wins_over_next_to_rom() {
        let r = p(&[
            "roms/gb/Tetris.gb",
            "roms/gb/Tetris.sav",
            "roms/gb/saves/Tetris.sav",
        ]);
        assert_eq!(r[0].save_paths, vec!["roms/gb/saves/Tetris.sav"]);
    }

    #[test]
    fn rom_without_save_defaults_to_saves_subdir() {
        let r = p(&["roms/nes/Zelda.nes"]);
        assert_eq!(r[0].save_dir, "roms/nes/saves");
        assert!(r[0].save_paths.is_empty());
        let root = p(&["Zelda.nes"]);
        assert_eq!(root[0].save_dir, "saves");
    }

    #[test]
    fn ngp_uses_fixed_dir_only() {
        let r = p(&[
            "roms/ngp/Sonic.ngp",
            "data/ngpds/Sonic.ngp.fla",
            "roms/ngp/saves/Sonic.ngp.fla",
        ]);
        assert_eq!(r[0].save_dir, NGP_SAVE_DIR);
        assert_eq!(r[0].save_paths, vec!["data/ngpds/Sonic.ngp.fla"]);
        assert_eq!(p(&["roms/ngp/Sonic.ngp"])[0].save_dir, NGP_SAVE_DIR);
    }

    #[test]
    fn sms_uses_full_rom_name() {
        let r = p(&[
            "roms/sms/Alex.sms",
            "roms/sms/saves/Alex.sms.sav",
            "roms/sms/saves/Alex.sav",
        ]);
        assert_eq!(r[0].save_paths, vec!["roms/sms/saves/Alex.sms.sav"]);
    }

    #[test]
    fn ambiguous_save_is_dropped_for_all_claimants() {
        let r = p(&[
            "roms/x/Game.nds",
            "roms/x/Game.gba",
            "roms/x/saves/Game.sav",
            "roms/x/saves/Game.sav1",
        ]);
        let nds = r.iter().find(|x| x.system == SystemId::Nds).unwrap();
        let gba = r.iter().find(|x| x.system == SystemId::Gba).unwrap();
        assert_eq!(nds.save_paths, vec!["roms/x/saves/Game.sav1"]);
        assert!(gba.save_paths.is_empty());
    }

    #[test]
    fn matching_is_case_insensitive_and_keeps_disk_case() {
        let r = p(&["roms/NDS/Mario.NDS", "roms/NDS/Saves/mario.sav"]);
        assert_eq!(r[0].save_dir, "roms/NDS/Saves");
        assert_eq!(r[0].save_paths, vec!["roms/NDS/Saves/mario.sav"]);
    }

    #[test]
    fn internal_and_non_rom_files_are_ignored() {
        let r = p(&[
            "_nds/nds-bootstrap-release.nds",
            "roms/readme.txt",
            "./roms/gb/A.gb",
            "/roms/gb/A.gb",
        ]);
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].rom_path, "roms/gb/A.gb");
    }

    #[test]
    fn hidden_and_appledouble_files_are_ignored() {
        let r = p(&[
            "roms/gba/Zelda.gba",
            "roms/gba/._Zelda.gba",
            "roms/gba/.Hidden.gba",
            "roms/gba/Zelda.sav",
            "roms/gba/._Zelda.sav",
            "roms/nds/Mario.nds",
            "roms/nds/saves/.Mario.sav",
            ".Trashes/501/Zelda.gba",
            "roms/.hidden/Tetris.gb",
        ]);
        assert_eq!(r.len(), 2);
        assert_eq!(r[0].rom_path, "roms/gba/Zelda.gba");
        assert_eq!(r[0].save_paths, vec!["roms/gba/Zelda.sav"]);
        assert_eq!(r[1].rom_path, "roms/nds/Mario.nds");
        assert!(r[1].save_paths.is_empty());
    }

    #[test]
    fn output_is_sorted_by_rom_path() {
        let r = p(&["roms/b.gb", "roms/a.gb"]);
        assert_eq!(r[0].rom_path, "roms/a.gb");
        assert_eq!(r[1].rom_path, "roms/b.gb");
    }
}
