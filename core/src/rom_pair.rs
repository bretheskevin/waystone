use alloc::collections::{BTreeMap, BTreeSet};
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::model::SystemId;
use crate::retroarch::{
    CoreInfo, SaveLayout, SaveRoot, core_save_folder, is_foreign_core_folder, ranked_cores,
    sorted_save_dir,
};
use crate::rom_flavor::{EXTERNAL_SAVE_ROOT, SaveFlavor, classify_flavored};
use crate::rom_systems::{GLOBAL_SAVE_DIR, base_name, parent_dir, rom_system, rom_system_for_file};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RomPairing {
    pub system: SystemId,
    pub rom_path: String,
    pub save_dir: String,
    pub save_paths: Vec<String>,
    pub flavor: SaveFlavor,
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

struct Candidate {
    dir: String,
    flavor: SaveFlavor,
    external_sub: bool,
}

fn is_external(path: &str) -> bool {
    path.strip_prefix(EXTERNAL_SAVE_ROOT)
        .is_some_and(|rest| rest.is_empty() || rest.starts_with('/'))
}

/// The external flavor when it can hold saves for `system` (else the ROM is native-only).
fn external_for(external: Option<SaveFlavor>, system: SystemId) -> Option<SaveFlavor> {
    external.filter(|f| f.supports(system))
}

fn external_candidates(flavor: SaveFlavor, subdirs: &BTreeSet<String>) -> Vec<Candidate> {
    let mut out = vec![Candidate {
        dir: EXTERNAL_SAVE_ROOT.to_string(),
        flavor,
        external_sub: false,
    }];
    out.extend(subdirs.iter().map(|d| Candidate {
        dir: d.clone(),
        flavor,
        external_sub: true,
    }));
    out
}

fn native_candidates(system: SystemId, rom: &str) -> impl Iterator<Item = Candidate> {
    save_dir_candidates(system, rom)
        .into_iter()
        .map(|dir| Candidate {
            dir,
            flavor: SaveFlavor::Native,
            external_sub: false,
        })
}

fn native_restore_target(system: SystemId, rom: &str) -> (String, SaveFlavor) {
    (
        save_dir_candidates(system, rom).swap_remove(0),
        SaveFlavor::Native,
    )
}

pub fn pair_roms(paths: &[String]) -> Vec<RomPairing> {
    pair_roms_external(paths, None)
}

/// [pair_roms] plus an optional external save tree listed under `@saves/` (depth 0 and 1).
/// External candidates come first; a save found in more than one external sub-folder is ambiguous
/// (none chosen). With no save, the restore target is the external root when the flavor supports
/// the system.
pub fn pair_roms_external(paths: &[String], external: Option<SaveFlavor>) -> Vec<RomPairing> {
    pair_with(
        paths,
        1,
        |system, rom, subdirs| {
            let mut out = external_for(external, system)
                .map(|f| external_candidates(f, subdirs))
                .unwrap_or_default();
            out.extend(native_candidates(system, rom));
            out
        },
        |system, rom, _| match external_for(external, system) {
            Some(f) => (EXTERNAL_SAVE_ROOT.to_string(), f),
            None => native_restore_target(system, rom),
        },
    )
}

/// RetroArch pairing driven by the parsed `retroarch.cfg` layout and installed cores. A `Dir`
/// save root is listed by the shell under `@saves/` (sub-folders up to depth 2); `ContentDir`
/// looks next to the ROM (and in its sorted sub-folders). Native candidates stay as fallback.
/// With no save, the restore target is where RetroArch itself would write it.
pub fn pair_roms_retroarch(
    paths: &[String],
    layout: &SaveLayout,
    installed: &[CoreInfo],
) -> Vec<RomPairing> {
    let ra = SaveFlavor::RetroArch;
    fn base<'a>(layout: &SaveLayout, rom: &'a str) -> &'a str {
        match layout.root {
            SaveRoot::Dir(_) => EXTERNAL_SAVE_ROOT,
            SaveRoot::ContentDir => parent_dir(rom),
        }
    }
    pair_with(
        paths,
        2,
        |system, rom, subdirs| {
            let mut out = Vec::new();
            if ra.supports(system) {
                out = match layout.root {
                    SaveRoot::Dir(_) => {
                        retroarch_dir_candidates(layout, installed, system, rom, subdirs)
                    }
                    SaveRoot::ContentDir => {
                        retroarch_content_candidates(layout, installed, system, rom)
                    }
                };
            }
            out.extend(native_candidates(system, rom));
            out
        },
        |system, rom, dirs| {
            if !ra.supports(system) {
                return native_restore_target(system, rom);
            }
            let parent = sorted_save_dir(layout, base(layout, rom), None, rom);
            let existing: Vec<String> = dirs
                .iter()
                .filter(|d| parent_dir(d).eq_ignore_ascii_case(&parent))
                .map(|d| base_name(d).to_string())
                .collect();
            let core = core_save_folder(system, installed, &existing);
            (
                sorted_save_dir(layout, base(layout, rom), core.as_deref(), rom),
                ra,
            )
        },
    )
}

/// `@saves/` tree candidates; with sorting on, the sub-folders RetroArch reads come before the
/// flat root, and folders it cannot use for this ROM are skipped: installed cores that cannot
/// run `system`, and (content sorting) folders outside the ROM's content-dir folder.
fn retroarch_dir_candidates(
    layout: &SaveLayout,
    installed: &[CoreInfo],
    system: SystemId,
    rom: &str,
    subdirs: &BTreeSet<String>,
) -> Vec<Candidate> {
    let content = sorted_save_dir(layout, EXTERNAL_SAVE_ROOT, None, rom);
    let mut out = external_candidates(SaveFlavor::RetroArch, subdirs);
    out.retain(|c| {
        let foreign_core =
            layout.sort_by_core && is_foreign_core_folder(system, installed, base_name(&c.dir));
        let other_content = layout.sort_by_content
            && !c.dir.eq_ignore_ascii_case(&content)
            && !parent_dir(&c.dir).eq_ignore_ascii_case(&content);
        !c.external_sub || !(foreign_core || other_content)
    });
    if layout.sort_by_core || layout.sort_by_content {
        out.rotate_left(1);
    }
    out
}

/// Content-dir candidates: each installed core's sorted folder, the content-sorted folder, then
/// the ROM's own folder.
fn retroarch_content_candidates(
    layout: &SaveLayout,
    installed: &[CoreInfo],
    system: SystemId,
    rom: &str,
) -> Vec<Candidate> {
    let ra = SaveFlavor::RetroArch;
    let dir = parent_dir(rom);
    let mut out: Vec<Candidate> = Vec::new();
    if layout.sort_by_core {
        out.extend(
            ranked_cores(system, installed)
                .into_iter()
                .map(|core| Candidate {
                    dir: sorted_save_dir(layout, dir, Some(core), rom),
                    flavor: ra,
                    external_sub: true,
                }),
        );
    }
    let by_content = sorted_save_dir(layout, dir, None, rom);
    if by_content != dir {
        out.push(Candidate {
            dir: by_content,
            flavor: ra,
            external_sub: false,
        });
    }
    out.push(Candidate {
        dir: dir.to_string(),
        flavor: ra,
        external_sub: false,
    });
    out
}

/// Shared pairing: `candidates` lists save dirs per ROM in priority order, `restore` picks the
/// target when none holds a save (given every listed folder, on-disk spelling). External
/// sub-folders are collected up to `max_sub_depth`.
fn pair_with(
    paths: &[String],
    max_sub_depth: usize,
    candidates: impl Fn(SystemId, &str, &BTreeSet<String>) -> Vec<Candidate>,
    restore: impl Fn(SystemId, &str, &[&str]) -> (String, SaveFlavor),
) -> Vec<RomPairing> {
    let mut by_dir: BTreeMap<String, Vec<&str>> = BTreeMap::new();
    let mut roms: Vec<(SystemId, &str)> = Vec::new();
    let mut subdirs: BTreeSet<String> = BTreeSet::new();
    for raw in paths {
        let p = clean(raw);
        if p.is_empty() || is_hidden(p) {
            continue;
        }
        let dir = parent_dir(p);
        by_dir.entry(dir.to_ascii_lowercase()).or_default().push(p);
        if is_external(p) {
            if (1..=max_sub_depth).contains(&dir.matches('/').count()) {
                subdirs.insert(dir.to_string());
            }
            continue;
        }
        if !is_internal(p)
            && let Some(rs) = rom_system_for_file(p)
        {
            roms.push((rs.system, p));
        }
    }
    roms.sort_by(|a, b| a.1.cmp(b.1));
    roms.dedup_by(|a, b| a.1 == b.1);

    let mut found: Vec<Vec<(Candidate, Vec<String>)>> = Vec::with_capacity(roms.len());
    let mut claims: BTreeMap<String, usize> = BTreeMap::new();
    for &(system, rom) in &roms {
        let rom_name = base_name(rom);
        let mut per_dir = Vec::new();
        let mut claimed: BTreeSet<String> = BTreeSet::new();
        for c in candidates(system, rom, &subdirs) {
            let mut hits: Vec<String> = by_dir
                .get(&c.dir.to_ascii_lowercase())
                .map(|files| {
                    files
                        .iter()
                        .filter(|f| {
                            **f != rom
                                && classify_flavored(c.flavor, system, rom_name, base_name(f))
                                    .is_some()
                        })
                        .map(|f| f.to_string())
                        .collect()
                })
                .unwrap_or_default();
            hits.sort();
            hits.dedup();
            claimed.extend(hits.iter().map(|h| h.to_ascii_lowercase()));
            per_dir.push((c, hits));
        }
        for h in claimed {
            *claims.entry(h).or_insert(0) += 1;
        }
        found.push(per_dir);
    }
    let dirs: Vec<&str> = by_dir.values().map(|files| parent_dir(files[0])).collect();

    roms.iter()
        .zip(found)
        .map(|(&(system, rom), per_dir)| {
            let unique: Vec<(Candidate, Vec<String>)> = per_dir
                .into_iter()
                .map(|(c, hits)| {
                    let hits = hits
                        .into_iter()
                        .filter(|h| claims.get(&h.to_ascii_lowercase()) == Some(&1))
                        .collect::<Vec<_>>();
                    (c, hits)
                })
                .collect();
            let sub_hits = unique
                .iter()
                .filter(|(c, h)| c.external_sub && !h.is_empty())
                .count();
            let chosen = unique
                .into_iter()
                .find(|(c, h)| !h.is_empty() && !(c.external_sub && sub_hits > 1));
            match chosen {
                Some((c, save_paths)) => RomPairing {
                    system,
                    rom_path: rom.to_string(),
                    save_dir: parent_dir(&save_paths[0]).to_string(),
                    save_paths,
                    flavor: c.flavor,
                },
                None => {
                    let (save_dir, flavor) = restore(system, rom, &dirs);
                    RomPairing {
                        system,
                        rom_path: rom.to_string(),
                        save_dir,
                        save_paths: Vec::new(),
                        flavor,
                    }
                }
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

    #[test]
    fn retroarch_saves_dir_is_not_a_native_candidate() {
        let r = p(&["roms/psx/Crash.cue", "retroarch/saves/Crash.srm"]);
        assert!(r[0].save_paths.is_empty());
        assert_eq!(GLOBAL_SAVE_DIR, "_nds/TWiLightMenu/saves");
    }

    fn ra_layout(root: SaveRoot, by_core: bool, by_content: bool) -> SaveLayout {
        SaveLayout {
            root,
            sort_by_core: by_core,
            sort_by_content: by_content,
            cores_dir: "retroarch/cores".to_string(),
            info_dir: "retroarch/info".to_string(),
        }
    }

    fn user_layout() -> SaveLayout {
        ra_layout(
            SaveRoot::Dir("retroarch/cores/savefiles".to_string()),
            true,
            false,
        )
    }

    fn ra_cores() -> Vec<CoreInfo> {
        [("mGBA", "gba|gb|gbc"), ("melonDS", "nds|ids|dsi")]
            .iter()
            .map(|(n, e)| CoreInfo {
                library_name: n.to_string(),
                extensions: e.split('|').map(String::from).collect(),
            })
            .collect()
    }

    fn pra(list: &[&str], layout: &SaveLayout) -> Vec<RomPairing> {
        pair_roms_retroarch(
            &list.iter().map(|s| s.to_string()).collect::<Vec<_>>(),
            layout,
            &ra_cores(),
        )
    }

    #[test]
    fn retroarch_layout_finds_core_sorted_save() {
        let r = pra(
            &[
                "roms/gba/Pokemon CHROME.gba",
                "@saves/mGBA/Pokemon CHROME.srm",
                "@saves/mGBA/._Pokemon CHROME.srm",
            ],
            &user_layout(),
        );
        assert_eq!(r.len(), 1);
        assert_eq!(r[0].save_paths, vec!["@saves/mGBA/Pokemon CHROME.srm"]);
        assert_eq!(r[0].save_dir, "@saves/mGBA");
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
    }

    #[test]
    fn retroarch_layout_restore_targets_core_folder() {
        let gba = pra(&["roms/gba/Pokemon CHROME.gba"], &user_layout());
        assert!(gba[0].save_paths.is_empty());
        assert_eq!(gba[0].save_dir, "@saves/mGBA");
        assert_eq!(gba[0].flavor, SaveFlavor::RetroArch);
        let nds = pra(&["roms/nds/Mario.nds"], &user_layout());
        assert_eq!(nds[0].save_dir, "@saves/melonDS");
        let flat = ra_layout(SaveRoot::Dir("ra/saves".to_string()), false, false);
        assert_eq!(pra(&["roms/gba/X.gba"], &flat)[0].save_dir, "@saves");
    }

    #[test]
    fn retroarch_layout_core_and_content_sorting() {
        let l = ra_layout(SaveRoot::Dir("ra/saves".to_string()), true, true);
        let none = pra(&["roms/gba/X.gba"], &l);
        assert_eq!(none[0].save_dir, "@saves/gba/mGBA");
        let found = pra(&["roms/gba/X.gba", "@saves/gba/mGBA/X.srm"], &l);
        assert_eq!(found[0].save_paths, vec!["@saves/gba/mGBA/X.srm"]);
        assert_eq!(found[0].save_dir, "@saves/gba/mGBA");
        let spelled = pra(&["roms/gba/X.gba", "@saves/gba/mgba/Other.srm"], &l);
        assert_eq!(spelled[0].save_dir, "@saves/gba/mgba");
    }

    #[test]
    fn retroarch_sorted_folder_wins_over_stale_flat_save() {
        let r = pra(
            &["roms/gba/X.gba", "@saves/X.srm", "@saves/mGBA/X.srm"],
            &user_layout(),
        );
        assert_eq!(r[0].save_paths, vec!["@saves/mGBA/X.srm"]);
        let flat = ra_layout(SaveRoot::Dir("ra/saves".to_string()), false, false);
        let r = pra(
            &["roms/gba/X.gba", "@saves/X.srm", "@saves/mGBA/X.srm"],
            &flat,
        );
        assert_eq!(r[0].save_paths, vec!["@saves/X.srm"]);
    }

    #[test]
    fn retroarch_same_stem_other_system_core_folder_is_not_ambiguous() {
        let cores = vec![
            CoreInfo {
                library_name: "Gambatte".to_string(),
                extensions: vec!["gb".to_string()],
            },
            CoreInfo {
                library_name: "Nestopia".to_string(),
                extensions: vec!["nes".to_string()],
            },
        ];
        let list: Vec<String> = [
            "roms/gb/Tetris.gb",
            "roms/nes/Tetris.nes",
            "@saves/Gambatte/Tetris.srm",
            "@saves/Nestopia/Tetris.srm",
        ]
        .iter()
        .map(|s| s.to_string())
        .collect();
        let r = pair_roms_retroarch(&list, &user_layout(), &cores);
        assert_eq!(r[0].save_paths, vec!["@saves/Gambatte/Tetris.srm"]);
        assert_eq!(r[1].save_paths, vec!["@saves/Nestopia/Tetris.srm"]);
    }

    #[test]
    fn retroarch_content_sorted_folders_of_other_systems_are_skipped() {
        let l = ra_layout(SaveRoot::Dir("ra/saves".to_string()), false, true);
        let r = pra(
            &[
                "roms/gb/Tetris.gb",
                "roms/nes/Tetris.nes",
                "@saves/gb/Tetris.srm",
                "@saves/nes/Tetris.srm",
            ],
            &l,
        );
        assert_eq!(r[0].save_paths, vec!["@saves/gb/Tetris.srm"]);
        assert_eq!(r[1].save_paths, vec!["@saves/nes/Tetris.srm"]);
    }

    #[test]
    fn retroarch_content_dir_restore_reuses_existing_core_folder() {
        let cores = vec![
            CoreInfo {
                library_name: "mGBA".to_string(),
                extensions: vec!["gba".to_string()],
            },
            CoreInfo {
                library_name: "gpSP".to_string(),
                extensions: vec!["gba".to_string()],
            },
        ];
        let list: Vec<String> = ["roms/gba/X.gba", "roms/gba/gpsp/Other.srm"]
            .iter()
            .map(|s| s.to_string())
            .collect();
        let l = ra_layout(SaveRoot::ContentDir, true, false);
        let r = pair_roms_retroarch(&list, &l, &cores);
        assert_eq!(r[0].save_dir, "roms/gba/gpsp");
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
    }

    #[test]
    fn retroarch_content_dir_is_sorted_too() {
        let l = ra_layout(SaveRoot::ContentDir, true, false);
        let r = pra(&["roms/gba/X.gba", "roms/gba/mGBA/X.srm"], &l);
        assert_eq!(r[0].save_paths, vec!["roms/gba/mGBA/X.srm"]);
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
        assert_eq!(pra(&["roms/gba/X.gba"], &l)[0].save_dir, "roms/gba/mGBA");
        let both = ra_layout(SaveRoot::ContentDir, true, true);
        let r = pra(&["roms/gba/X.gba", "roms/gba/gba/mGBA/X.srm"], &both);
        assert_eq!(r[0].save_paths, vec!["roms/gba/gba/mGBA/X.srm"]);
        assert_eq!(
            pra(&["roms/gba/Y.gba"], &both)[0].save_dir,
            "roms/gba/gba/mGBA"
        );
    }

    #[test]
    fn retroarch_content_dir_pairs_next_to_rom() {
        let l = ra_layout(SaveRoot::ContentDir, false, false);
        let r = pra(
            &["roms/gba/X.gba", "roms/gba/X.srm", "roms/gba/._X.srm"],
            &l,
        );
        assert_eq!(r[0].save_paths, vec!["roms/gba/X.srm"]);
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
        let none = pra(&["roms/gba/X.gba"], &l);
        assert_eq!(none[0].save_dir, "roms/gba");
        assert_eq!(none[0].flavor, SaveFlavor::RetroArch);
    }

    #[test]
    fn retroarch_content_dir_native_sav_fallback_reports_native() {
        let l = ra_layout(SaveRoot::ContentDir, false, false);
        let r = pra(&["roms/gba/X.gba", "roms/gba/X.sav"], &l);
        assert_eq!(r[0].save_paths, vec!["roms/gba/X.sav"]);
        assert_eq!(r[0].flavor, SaveFlavor::Native);
    }

    #[test]
    fn retroarch_content_dir_srm_shared_with_native_naming_is_not_ambiguous() {
        let l = ra_layout(SaveRoot::ContentDir, false, false);
        let r = pra(&["roms/snes/Mario.sfc", "roms/snes/Mario.srm"], &l);
        assert_eq!(r[0].save_paths, vec!["roms/snes/Mario.srm"]);
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
    }

    fn px(list: &[&str], flavor: Option<SaveFlavor>) -> Vec<RomPairing> {
        pair_roms_external(
            &list.iter().map(|s| s.to_string()).collect::<Vec<_>>(),
            flavor,
        )
    }

    #[test]
    fn retroarch_external_root_depth0() {
        let r = px(
            &["roms/gba/Zelda.gba", "@saves/Zelda.srm"],
            Some(SaveFlavor::RetroArch),
        );
        assert_eq!(r[0].save_dir, "@saves");
        assert_eq!(r[0].save_paths, vec!["@saves/Zelda.srm"]);
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
    }

    #[test]
    fn retroarch_per_core_subfolder_depth1() {
        let r = px(
            &["Zelda.gba", "@saves/mGBA/Zelda.srm"],
            Some(SaveFlavor::RetroArch),
        );
        assert_eq!(r[0].save_dir, "@saves/mGBA");
        assert_eq!(r[0].save_paths, vec!["@saves/mGBA/Zelda.srm"]);
    }

    #[test]
    fn save_in_two_core_subfolders_is_ambiguous() {
        let r = px(
            &[
                "Zelda.gba",
                "@saves/mGBA/Zelda.srm",
                "@saves/gpSP/Zelda.srm",
            ],
            Some(SaveFlavor::RetroArch),
        );
        assert!(r[0].save_paths.is_empty());
        assert_eq!(r[0].save_dir, "@saves");
        assert_eq!(r[0].flavor, SaveFlavor::RetroArch);
    }

    #[test]
    fn external_wins_over_native_and_native_is_fallback() {
        let both = px(
            &["roms/Zelda.gba", "roms/Zelda.sav", "@saves/Zelda.srm"],
            Some(SaveFlavor::RetroArch),
        );
        assert_eq!(both[0].save_paths, vec!["@saves/Zelda.srm"]);
        let native_only = px(
            &["roms/Zelda.gba", "roms/Zelda.sav"],
            Some(SaveFlavor::RetroArch),
        );
        assert_eq!(native_only[0].save_paths, vec!["roms/Zelda.sav"]);
        assert_eq!(native_only[0].flavor, SaveFlavor::Native);
    }

    #[test]
    fn empty_external_dir_is_the_restore_target_only_when_supported() {
        let nds = px(&["roms/Mario.nds"], Some(SaveFlavor::Drastic));
        assert_eq!(
            (nds[0].save_dir.as_str(), nds[0].flavor),
            ("@saves", SaveFlavor::Drastic)
        );
        let gba = px(&["roms/Zelda.gba"], Some(SaveFlavor::Drastic));
        assert_eq!(
            (gba[0].save_dir.as_str(), gba[0].flavor),
            ("roms/saves", SaveFlavor::Native)
        );
    }

    #[test]
    fn external_paths_are_never_roms_and_pair_roms_is_unchanged() {
        assert!(px(&["@saves/Fake.gba"], Some(SaveFlavor::RetroArch)).is_empty());
        let r = p(&["roms/gba/Zelda.gba", "roms/gba/Zelda.sav"]);
        assert_eq!(r[0].flavor, SaveFlavor::Native);
        assert_eq!(r[0].save_paths, vec!["roms/gba/Zelda.sav"]);
    }

    #[test]
    fn drastic_dsv_pairs_from_backup_dir() {
        let r = px(
            &["roms/nds/Mario.nds", "@saves/Mario.dsv"],
            Some(SaveFlavor::Drastic),
        );
        assert_eq!(r[0].save_paths, vec!["@saves/Mario.dsv"]);
    }
}
