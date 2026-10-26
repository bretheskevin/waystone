use waystone_core::rom_flavor::SaveFlavor;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct EmulatorPreset {
    pub id: &'static str,
    pub label: &'static str,
    pub flavor: SaveFlavor,
    pub needs_save_dir: bool,
}

pub const PRESETS: &[EmulatorPreset] = &[
    EmulatorPreset {
        id: "retroarch",
        label: "RetroArch",
        flavor: SaveFlavor::RetroArch,
        needs_save_dir: true,
    },
    EmulatorPreset {
        id: "melonds",
        label: "melonDS",
        flavor: SaveFlavor::Native,
        needs_save_dir: false,
    },
    EmulatorPreset {
        id: "drastic",
        label: "DraStic",
        flavor: SaveFlavor::Drastic,
        needs_save_dir: true,
    },
];

pub fn preset(id: &str) -> Option<&'static EmulatorPreset> {
    PRESETS.iter().find(|p| p.id == id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lookup_by_id() {
        assert_eq!(preset("retroarch").unwrap().flavor, SaveFlavor::RetroArch);
        assert!(preset("melonds").is_some_and(|p| !p.needs_save_dir));
        assert!(preset("nope").is_none());
    }
}
