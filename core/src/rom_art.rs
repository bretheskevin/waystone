use alloc::string::String;
use alloc::vec::Vec;

use crate::model::SystemId;

pub const TWILIGHT_BOXART_DIR: &str = "_nds/TWiLightMenu/boxart/";

/// TWiLight Menu++ box art, SD-root-relative, in its lookup order (`<rom file name>.bmp`, then `.png`).
pub fn twilight_boxart_paths(rom_file_name: &str) -> Vec<String> {
    vec![
        format!("{TWILIGHT_BOXART_DIR}{rom_file_name}.bmp"),
        format!("{TWILIGHT_BOXART_DIR}{rom_file_name}.png"),
    ]
}

/// libretro-thumbnails playlist (database) name for a ROM; None for consoles without one.
pub fn retroarch_playlist(system: SystemId, rom_file_name: &str) -> Option<&'static str> {
    Some(match system {
        SystemId::Nds => "Nintendo - Nintendo DS",
        SystemId::Gba => "Nintendo - Game Boy Advance",
        SystemId::Gbc => "Nintendo - Game Boy Color",
        SystemId::Gb => "Nintendo - Game Boy",
        SystemId::Nes => "Nintendo - Nintendo Entertainment System",
        SystemId::Snes => "Nintendo - Super Nintendo Entertainment System",
        SystemId::N64 => "Nintendo - Nintendo 64",
        SystemId::Md => "Sega - Mega Drive - Genesis",
        SystemId::Sms => "Sega - Master System - Mark III",
        SystemId::Gg => "Sega - Game Gear",
        SystemId::Ngp if rom_file_name.to_ascii_lowercase().ends_with(".ngc") => {
            "SNK - Neo Geo Pocket Color"
        }
        SystemId::Ngp => "SNK - Neo Geo Pocket",
        SystemId::Psx => "Sony - PlayStation",
        SystemId::Switch | SystemId::ThreeDS => return None,
    })
}

/// RetroArch thumbnail file-name rule: each of `&*/:`<>?\|` becomes `_`.
pub fn retroarch_thumbnail_label(label: &str) -> String {
    label
        .chars()
        .map(|c| if "&*/:`<>?\\|".contains(c) { '_' } else { c })
        .collect()
}

/// `thumbnails/<playlist>/Named_Boxarts/<sanitized ROM stem>.png`, relative to the RetroArch root.
pub fn retroarch_boxart_path(system: SystemId, rom_file_name: &str) -> Option<String> {
    let playlist = retroarch_playlist(system, rom_file_name)?;
    let stem = rom_file_name
        .rfind('.')
        .map_or(rom_file_name, |i| &rom_file_name[..i]);
    Some(format!(
        "thumbnails/{playlist}/Named_Boxarts/{}.png",
        retroarch_thumbnail_label(stem)
    ))
}

/// Short system label for ROM rows; mirrors shell-common `rom_system_badge`.
pub fn system_badge(system: &str) -> &'static str {
    match system {
        "nds" => "DS",
        "gba" => "GBA",
        "gb" => "GB",
        "nes" => "NES",
        "snes" => "SNES",
        "md" => "MD",
        "sms" => "SMS",
        "gg" => "GG",
        "ngp" => "NGP",
        "n64" => "N64",
        "psx" => "PSX",
        _ => "ROM",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::string::ToString;

    #[test]
    fn twilight_paths_use_full_file_name_bmp_then_png() {
        assert_eq!(
            twilight_boxart_paths("Zelda (U).gba"),
            vec![
                "_nds/TWiLightMenu/boxart/Zelda (U).gba.bmp".to_string(),
                "_nds/TWiLightMenu/boxart/Zelda (U).gba.png".to_string(),
            ]
        );
    }

    #[test]
    fn retroarch_path_uses_playlist_and_sanitized_stem() {
        assert_eq!(
            retroarch_boxart_path(SystemId::Gba, "Mario & Luigi: Superstar Saga (USA).gba")
                .as_deref(),
            Some(
                "thumbnails/Nintendo - Game Boy Advance/Named_Boxarts/Mario _ Luigi_ Superstar Saga (USA).png"
            )
        );
        assert_eq!(retroarch_thumbnail_label("a&*/:`<>?\\|b"), "a__________b");
        assert_eq!(retroarch_boxart_path(SystemId::Switch, "x.nsp"), None);
        assert_eq!(
            retroarch_playlist(SystemId::Ngp, "Metal Slug.ngc"),
            Some("SNK - Neo Geo Pocket Color")
        );
        assert_eq!(
            retroarch_playlist(SystemId::Ngp, "Pac.ngp"),
            Some("SNK - Neo Geo Pocket")
        );
    }

    #[test]
    fn badges_match_shell_common_rom_system_badge() {
        let expected = [
            ("nds", "DS"),
            ("gba", "GBA"),
            ("gb", "GB"),
            ("nes", "NES"),
            ("snes", "SNES"),
            ("md", "MD"),
            ("sms", "SMS"),
            ("gg", "GG"),
            ("ngp", "NGP"),
            ("n64", "N64"),
            ("psx", "PSX"),
            ("gbc", "ROM"),
            ("zzz", "ROM"),
        ];
        for (sys, badge) in expected {
            assert_eq!(system_badge(sys), badge, "{sys}");
        }
    }
}
