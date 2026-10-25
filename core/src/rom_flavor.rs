//! Per-emulator save naming for ROM-keyed sync (Android presets). Native = rom_systems table.
use alloc::format;
use alloc::string::{String, ToString};

use crate::model::SystemId;
use crate::rom_systems::{
    SaveFileKind, base_name, classify_save, file_stem, native_save_name, strip_prefix_ci,
};

/// Virtual path prefix under which shells list an external save directory (never a ROM).
pub const EXTERNAL_SAVE_ROOT: &str = "@saves";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveFlavor {
    Native,
    RetroArch,
    Drastic,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveCodec {
    Raw,
    Dsv,
}

type Suffixes = &'static [(&'static str, SaveCodec)];

impl SaveFlavor {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "native" => Some(Self::Native),
            "retroarch" => Some(Self::RetroArch),
            "drastic" => Some(Self::Drastic),
            _ => None,
        }
    }

    pub fn as_str(self) -> &'static str {
        match self {
            Self::Native => "native",
            Self::RetroArch => "retroarch",
            Self::Drastic => "drastic",
        }
    }

    // First entry is what a restore writes when no local file exists. None = rom_systems table.
    fn battery_suffixes(self, system: SystemId) -> Option<Suffixes> {
        match (self, system) {
            (Self::Native, _) => None,
            (Self::RetroArch, SystemId::Nds) => {
                Some(&[(".srm", SaveCodec::Raw), (".dsv", SaveCodec::Dsv)])
            }
            (Self::RetroArch, _) => Some(&[(".srm", SaveCodec::Raw)]),
            (Self::Drastic, SystemId::Nds) => Some(&[(".dsv", SaveCodec::Dsv)]),
            (Self::Drastic, _) => Some(&[]),
        }
    }

    pub fn supports(self, system: SystemId) -> bool {
        self.battery_suffixes(system).is_none_or(|s| !s.is_empty())
    }
}

fn rom_stem(rom_file_name: &str) -> &str {
    file_stem(base_name(rom_file_name))
}

pub fn classify_flavored(
    flavor: SaveFlavor,
    system: SystemId,
    rom_file_name: &str,
    save_file_name: &str,
) -> Option<(SaveFileKind, SaveCodec)> {
    match flavor.battery_suffixes(system) {
        None => classify_save(system, rom_file_name, save_file_name).map(|k| (k, SaveCodec::Raw)),
        Some(suffixes) => {
            let rest = strip_prefix_ci(base_name(save_file_name), rom_stem(rom_file_name))?;
            suffixes
                .iter()
                .find(|(s, _)| rest.eq_ignore_ascii_case(s))
                .map(|&(_, codec)| (SaveFileKind::Battery, codec))
        }
    }
}

pub fn flavored_native_name(
    flavor: SaveFlavor,
    system: SystemId,
    rom_file_name: &str,
    kind: &SaveFileKind,
    existing_name: Option<&str>,
) -> Option<(String, SaveCodec)> {
    match flavor.battery_suffixes(system) {
        None => native_save_name(system, rom_file_name, kind).map(|n| (n, SaveCodec::Raw)),
        Some(suffixes) => {
            if *kind != SaveFileKind::Battery {
                return None;
            }
            if let Some(name) = existing_name
                && let Some((SaveFileKind::Battery, codec)) =
                    classify_flavored(flavor, system, rom_file_name, name)
            {
                return Some((base_name(name).to_string(), codec));
            }
            let &(suffix, codec) = suffixes.first()?;
            Some((format!("{}{}", rom_stem(rom_file_name), suffix), codec))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_round_trips() {
        for f in [
            SaveFlavor::Native,
            SaveFlavor::RetroArch,
            SaveFlavor::Drastic,
        ] {
            assert_eq!(SaveFlavor::parse(f.as_str()), Some(f));
        }
        assert_eq!(SaveFlavor::parse("azahar"), None);
    }

    #[test]
    fn retroarch_uses_srm_for_every_system() {
        assert_eq!(
            classify_flavored(
                SaveFlavor::RetroArch,
                SystemId::Gba,
                "Zelda.gba",
                "Zelda.srm"
            ),
            Some((SaveFileKind::Battery, SaveCodec::Raw))
        );
        assert_eq!(
            classify_flavored(
                SaveFlavor::RetroArch,
                SystemId::Gba,
                "Zelda.gba",
                "Zelda.sav"
            ),
            None
        );
        assert_eq!(
            classify_flavored(
                SaveFlavor::RetroArch,
                SystemId::Nds,
                "Mario.nds",
                "Mario.dsv"
            ),
            Some((SaveFileKind::Battery, SaveCodec::Dsv))
        );
        assert_eq!(
            classify_flavored(
                SaveFlavor::RetroArch,
                SystemId::Nds,
                "Mario.nds",
                "Mario.sav1"
            ),
            None
        );
    }

    #[test]
    fn drastic_is_nds_dsv_only() {
        assert_eq!(
            classify_flavored(SaveFlavor::Drastic, SystemId::Nds, "Mario.nds", "MARIO.DSV"),
            Some((SaveFileKind::Battery, SaveCodec::Dsv))
        );
        assert!(!SaveFlavor::Drastic.supports(SystemId::Gba));
        assert!(SaveFlavor::Drastic.supports(SystemId::Nds));
    }

    #[test]
    fn native_flavor_delegates_to_classify_save() {
        assert_eq!(
            classify_flavored(SaveFlavor::Native, SystemId::Nds, "Mario.nds", "Mario.sav2"),
            Some((SaveFileKind::Slot(2), SaveCodec::Raw))
        );
    }

    #[test]
    fn native_name_prefers_the_existing_local_file() {
        let name = |kind: &SaveFileKind, existing| {
            flavored_native_name(
                SaveFlavor::RetroArch,
                SystemId::Nds,
                "Mario.nds",
                kind,
                existing,
            )
        };
        assert_eq!(
            name(&SaveFileKind::Battery, Some("Mario.dsv")),
            Some(("Mario.dsv".into(), SaveCodec::Dsv))
        );
        assert_eq!(
            name(&SaveFileKind::Battery, None),
            Some(("Mario.srm".into(), SaveCodec::Raw))
        );
        assert_eq!(name(&SaveFileKind::Slot(1), None), None);
    }
}
