use alloc::string::String;
use alloc::vec::Vec;

use crate::model::SystemId;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IdScheme {
    NdsHeader,
    GbaHeader,
    Crc32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveName {
    StemPlus(&'static str),
    RomNamePlus(&'static str),
}

#[derive(Debug)]
pub struct RomSystem {
    pub system: SystemId,
    pub rom_exts: &'static [&'static str],
    pub battery: SaveName,
    pub fixed_save_dir: Option<&'static str>,
    pub id_scheme: IdScheme,
}

pub const GLOBAL_SAVE_DIR: &str = "_nds/TWiLightMenu/saves";
pub const NGP_SAVE_DIR: &str = "data/ngpds";

// Save layouts from the DS-Homebrew wiki ds-index/emulators.md; NGP/SMS/GG/MD/SNES await hardware confirmation.
pub static ROM_SYSTEMS: &[RomSystem] = &[
    RomSystem {
        system: SystemId::Nds,
        rom_exts: &["nds", "dsi", "ids", "srl", "app", "ndz"],
        battery: SaveName::StemPlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::NdsHeader,
    },
    RomSystem {
        system: SystemId::Gba,
        rom_exts: &["gba", "agb", "mb"],
        battery: SaveName::StemPlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::GbaHeader,
    },
    RomSystem {
        system: SystemId::Gb,
        rom_exts: &["gb", "gbc", "sgb"],
        battery: SaveName::StemPlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Nes,
        rom_exts: &["nes", "fds"],
        battery: SaveName::StemPlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Snes,
        rom_exts: &["smc", "sfc"],
        battery: SaveName::StemPlus(".srm"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Md,
        rom_exts: &["gen", "md"],
        battery: SaveName::StemPlus(".srm"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Sms,
        rom_exts: &["sms"],
        battery: SaveName::RomNamePlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Gg,
        rom_exts: &["gg"],
        battery: SaveName::RomNamePlus(".sav"),
        fixed_save_dir: None,
        id_scheme: IdScheme::Crc32,
    },
    RomSystem {
        system: SystemId::Ngp,
        rom_exts: &["ngp", "ngc"],
        battery: SaveName::RomNamePlus(".fla"),
        fixed_save_dir: Some(NGP_SAVE_DIR),
        id_scheme: IdScheme::Crc32,
    },
];

pub fn rom_system(system: SystemId) -> Option<&'static RomSystem> {
    ROM_SYSTEMS.iter().find(|r| r.system == system)
}

pub fn base_name(path: &str) -> &str {
    path.rsplit('/').next().unwrap_or(path)
}

pub fn file_stem(file_name: &str) -> &str {
    match file_name.rfind('.') {
        Some(i) if i > 0 => &file_name[..i],
        _ => file_name,
    }
}

fn extension(file_name: &str) -> Option<&str> {
    match file_name.rfind('.') {
        Some(i) if i > 0 && i + 1 < file_name.len() => Some(&file_name[i + 1..]),
        _ => None,
    }
}

pub fn rom_system_for_file(file_name: &str) -> Option<&'static RomSystem> {
    let ext = extension(base_name(file_name))?;
    ROM_SYSTEMS
        .iter()
        .find(|r| r.rom_exts.iter().any(|e| e.eq_ignore_ascii_case(ext)))
}

fn strip_prefix_ci<'a>(s: &'a str, prefix: &str) -> Option<&'a str> {
    if s.len() < prefix.len() || !s.is_char_boundary(prefix.len()) {
        return None;
    }
    if s[..prefix.len()].eq_ignore_ascii_case(prefix) {
        Some(&s[prefix.len()..])
    } else {
        None
    }
}

fn single_digit(s: &str) -> Option<u8> {
    let b = s.as_bytes();
    if b.len() == 1 && (b'1'..=b'9').contains(&b[0]) {
        Some(b[0] - b'0')
    } else {
        None
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveFileKind {
    Battery,
    Slot(u8),
    DsiWare { n: Option<u8>, is_pub: bool },
}

impl SaveFileKind {
    pub fn slot(&self) -> String {
        match self {
            Self::Battery => "battery".into(),
            Self::Slot(n) => format!("slot-{}", n),
            Self::DsiWare { n: None, .. } => "dsiware".into(),
            Self::DsiWare { n: Some(n), .. } => format!("dsiware-{}", n),
        }
    }

    pub fn canonical_file_name(&self) -> String {
        match self {
            Self::DsiWare { is_pub, .. } => {
                format!("{}.{}", self.slot(), if *is_pub { "pub" } else { "prv" })
            }
            _ => self.slot(),
        }
    }
}

pub fn parse_canonical_file_name(name: &str) -> Option<SaveFileKind> {
    if name == "battery" {
        return Some(SaveFileKind::Battery);
    }
    if let Some(n) = name.strip_prefix("slot-") {
        return single_digit(n).map(SaveFileKind::Slot);
    }
    let (slot, is_pub) = if let Some(s) = name.strip_suffix(".pub") {
        (s, true)
    } else {
        (name.strip_suffix(".prv")?, false)
    };
    if slot == "dsiware" {
        return Some(SaveFileKind::DsiWare { n: None, is_pub });
    }
    let n = single_digit(slot.strip_prefix("dsiware-")?)?;
    Some(SaveFileKind::DsiWare { n: Some(n), is_pub })
}

pub fn classify_save(
    system: SystemId,
    rom_file_name: &str,
    save_file_name: &str,
) -> Option<SaveFileKind> {
    let rs = rom_system(system)?;
    let rom_file_name = base_name(rom_file_name);
    let save_file_name = base_name(save_file_name);
    match rs.battery {
        SaveName::RomNamePlus(suffix) => {
            let rest = strip_prefix_ci(save_file_name, rom_file_name)?;
            rest.eq_ignore_ascii_case(suffix)
                .then_some(SaveFileKind::Battery)
        }
        SaveName::StemPlus(suffix) => {
            let rest = strip_prefix_ci(save_file_name, file_stem(rom_file_name))?;
            if rest.eq_ignore_ascii_case(suffix) {
                return Some(SaveFileKind::Battery);
            }
            if system != SystemId::Nds {
                return None;
            }
            let lower = rest.to_ascii_lowercase();
            match lower.as_str() {
                ".pub" => {
                    return Some(SaveFileKind::DsiWare {
                        n: None,
                        is_pub: true,
                    });
                }
                ".prv" => {
                    return Some(SaveFileKind::DsiWare {
                        n: None,
                        is_pub: false,
                    });
                }
                _ => {}
            }
            if let Some(d) = lower.strip_prefix(".sav") {
                return single_digit(d).map(SaveFileKind::Slot);
            }
            if let Some(d) = lower.strip_prefix(".pu") {
                return single_digit(d).map(|n| SaveFileKind::DsiWare {
                    n: Some(n),
                    is_pub: true,
                });
            }
            if let Some(d) = lower.strip_prefix(".pr") {
                return single_digit(d).map(|n| SaveFileKind::DsiWare {
                    n: Some(n),
                    is_pub: false,
                });
            }
            None
        }
    }
}

pub fn native_save_name(
    system: SystemId,
    rom_file_name: &str,
    kind: &SaveFileKind,
) -> Option<String> {
    let rs = rom_system(system)?;
    let rom_file_name = base_name(rom_file_name);
    let stem = file_stem(rom_file_name);
    match (kind, rs.battery) {
        (SaveFileKind::Battery, SaveName::StemPlus(s)) => Some(format!("{}{}", stem, s)),
        (SaveFileKind::Battery, SaveName::RomNamePlus(s)) => {
            Some(format!("{}{}", rom_file_name, s))
        }
        _ if system != SystemId::Nds => None,
        (SaveFileKind::Slot(n), _) => Some(format!("{}.sav{}", stem, n)),
        (SaveFileKind::DsiWare { n: None, is_pub }, _) => {
            Some(format!("{}.{}", stem, if *is_pub { "pub" } else { "prv" }))
        }
        (SaveFileKind::DsiWare { n: Some(n), is_pub }, _) => Some(format!(
            "{}.{}{}",
            stem,
            if *is_pub { "pu" } else { "pr" },
            n
        )),
    }
}

pub fn slot_names(system: SystemId) -> Vec<String> {
    let mut v = vec![SaveFileKind::Battery.slot()];
    if system == SystemId::Nds {
        for n in 1..=9 {
            v.push(SaveFileKind::Slot(n).slot());
        }
        v.push(
            SaveFileKind::DsiWare {
                n: None,
                is_pub: true,
            }
            .slot(),
        );
        for n in 1..=9 {
            v.push(
                SaveFileKind::DsiWare {
                    n: Some(n),
                    is_pub: true,
                }
                .slot(),
            );
        }
    }
    v
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extension_lookup_is_case_insensitive() {
        assert_eq!(
            rom_system_for_file("roms/nds/Mario.NDS").unwrap().system,
            SystemId::Nds
        );
        assert_eq!(
            rom_system_for_file("Zelda.gbc").unwrap().system,
            SystemId::Gb
        );
        assert_eq!(
            rom_system_for_file("Sonic.gen").unwrap().system,
            SystemId::Md
        );
        assert_eq!(
            rom_system_for_file("Neo.ngc").unwrap().system,
            SystemId::Ngp
        );
        assert!(rom_system_for_file("Mario.sav").is_none());
        assert!(rom_system_for_file("noext").is_none());
        assert!(rom_system_for_file(".nds").is_none());
    }

    #[test]
    fn stem_and_base_name() {
        assert_eq!(base_name("a/b/Game.nds"), "Game.nds");
        assert_eq!(file_stem("Game (U).nds"), "Game (U)");
        assert_eq!(file_stem("Game"), "Game");
    }

    #[test]
    fn nds_slot_classification_preserves_twilight_semantics() {
        let rom = "Pokemon Diamond.nds";
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.sav"),
            Some(SaveFileKind::Battery)
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "pokemon diamond.SAV1"),
            Some(SaveFileKind::Slot(1))
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.pub"),
            Some(SaveFileKind::DsiWare {
                n: None,
                is_pub: true
            })
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.prv"),
            Some(SaveFileKind::DsiWare {
                n: None,
                is_pub: false
            })
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.pu3"),
            Some(SaveFileKind::DsiWare {
                n: Some(3),
                is_pub: true
            })
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.pr3"),
            Some(SaveFileKind::DsiWare {
                n: Some(3),
                is_pub: false
            })
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond.sav0"),
            None
        );
        assert_eq!(
            classify_save(SystemId::Nds, rom, "Pokemon Diamond Kart.sav"),
            None
        );
        assert_eq!(classify_save(SystemId::Nds, rom, "usrcheat.dat"), None);
    }

    #[test]
    fn non_nds_systems_only_have_battery() {
        assert_eq!(
            classify_save(SystemId::Gba, "Zelda.gba", "Zelda.sav"),
            Some(SaveFileKind::Battery)
        );
        assert_eq!(
            classify_save(SystemId::Gba, "Zelda.gba", "Zelda.sav1"),
            None
        );
        assert_eq!(
            classify_save(SystemId::Snes, "Mario.sfc", "Mario.srm"),
            Some(SaveFileKind::Battery)
        );
        assert_eq!(
            classify_save(SystemId::Snes, "Mario.sfc", "Mario.sav"),
            None
        );
        assert_eq!(
            classify_save(SystemId::Sms, "Alex.sms", "Alex.sms.sav"),
            Some(SaveFileKind::Battery)
        );
        assert_eq!(classify_save(SystemId::Sms, "Alex.sms", "Alex.sav"), None);
        assert_eq!(
            classify_save(SystemId::Ngp, "Sonic.ngp", "Sonic.ngp.fla"),
            Some(SaveFileKind::Battery)
        );
        assert_eq!(classify_save(SystemId::Switch, "x", "x.sav"), None);
    }

    #[test]
    fn native_names_follow_local_rom() {
        let b = SaveFileKind::Battery;
        assert_eq!(
            native_save_name(SystemId::Nds, "Mario (E).nds", &b).unwrap(),
            "Mario (E).sav"
        );
        assert_eq!(
            native_save_name(SystemId::Md, "Sonic.md", &b).unwrap(),
            "Sonic.srm"
        );
        assert_eq!(
            native_save_name(SystemId::Gg, "Shinobi.GG", &b).unwrap(),
            "Shinobi.GG.sav"
        );
        assert_eq!(
            native_save_name(SystemId::Ngp, "Sonic.ngp", &b).unwrap(),
            "Sonic.ngp.fla"
        );
        assert_eq!(
            native_save_name(SystemId::Nds, "A.nds", &SaveFileKind::Slot(2)).unwrap(),
            "A.sav2"
        );
        assert_eq!(
            native_save_name(
                SystemId::Nds,
                "A.nds",
                &SaveFileKind::DsiWare {
                    n: Some(4),
                    is_pub: false
                }
            )
            .unwrap(),
            "A.pr4"
        );
        assert_eq!(
            native_save_name(SystemId::Gba, "A.gba", &SaveFileKind::Slot(2)),
            None
        );
    }

    #[test]
    fn canonical_names_round_trip() {
        let kinds = [
            SaveFileKind::Battery,
            SaveFileKind::Slot(7),
            SaveFileKind::DsiWare {
                n: None,
                is_pub: true,
            },
            SaveFileKind::DsiWare {
                n: None,
                is_pub: false,
            },
            SaveFileKind::DsiWare {
                n: Some(2),
                is_pub: true,
            },
        ];
        for k in kinds {
            assert_eq!(parse_canonical_file_name(&k.canonical_file_name()), Some(k));
        }
        assert_eq!(
            SaveFileKind::DsiWare {
                n: Some(2),
                is_pub: false
            }
            .canonical_file_name(),
            "dsiware-2.prv"
        );
        assert_eq!(parse_canonical_file_name("slot-0"), None);
        assert_eq!(parse_canonical_file_name("Mario.sav"), None);
    }

    #[test]
    fn slot_names_per_system() {
        let nds = slot_names(SystemId::Nds);
        assert_eq!(nds.len(), 20);
        assert_eq!(nds[0], "battery");
        assert!(
            nds.contains(&"slot-9".into())
                && nds.contains(&"dsiware".into())
                && nds.contains(&"dsiware-9".into())
        );
        assert_eq!(slot_names(SystemId::Gba), vec![String::from("battery")]);
    }
}
