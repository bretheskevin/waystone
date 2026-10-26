//! RetroArch save layout from `retroarch.cfg` and installed cores' `.info` files (pure text in).
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::model::SystemId;
use crate::rom_systems::{base_name, parent_dir, rom_system};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SaveRoot {
    ContentDir,
    Dir(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SaveLayout {
    pub root: SaveRoot,
    pub sort_by_core: bool,
    pub sort_by_content: bool,
    pub cores_dir: String,
    pub info_dir: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CoreInfo {
    pub library_name: String,
    pub extensions: Vec<String>,
}

/// SD-root-relative directories RetroArch uses when the cfg says `"default"` or omits the key
/// (`frontend/drivers/platform_switch.c` / `platform_ctr.c`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RetroArchDefaults {
    pub save_dir: &'static str,
    pub cores_dir: &'static str,
    pub info_dir: &'static str,
}

pub const SWITCH_DEFAULTS: RetroArchDefaults = RetroArchDefaults {
    save_dir: "retroarch/cores/savefiles",
    cores_dir: "retroarch/cores",
    info_dir: "retroarch/info",
};

pub const CTR_DEFAULTS: RetroArchDefaults = RetroArchDefaults {
    save_dir: "retroarch/cores/savefiles",
    cores_dir: "retroarch/cores",
    info_dir: "retroarch/cores/info",
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RetroArchPlatform {
    Switch,
    Ctr,
}

impl RetroArchPlatform {
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "switch" => Some(Self::Switch),
            "3ds" => Some(Self::Ctr),
            _ => None,
        }
    }

    pub fn defaults(self) -> RetroArchDefaults {
        match self {
            Self::Switch => SWITCH_DEFAULTS,
            Self::Ctr => CTR_DEFAULTS,
        }
    }
}

/// Last `key = value` per key (quotes stripped); `#` comments and malformed lines ignored.
fn cfg_value<'a>(text: &'a str, key: &str) -> Option<&'a str> {
    text.lines().rev().find_map(|line| {
        let line = line.trim();
        if line.starts_with('#') {
            return None;
        }
        let (k, v) = line.split_once('=')?;
        if k.trim() != key {
            return None;
        }
        let v = v.trim();
        Some(
            v.strip_prefix('"')
                .and_then(|s| s.strip_suffix('"'))
                .unwrap_or(v),
        )
    })
}

/// SD-root-relative dir from a cfg path, else `default` (missing/empty/"default"/SD root).
fn cfg_dir(text: &str, key: &str, default: &str) -> String {
    cfg_value(text, key)
        .filter(|v| *v != "default")
        .map(|v| v.strip_prefix("sdmc:").unwrap_or(v).trim_matches('/'))
        .filter(|v| !v.is_empty())
        .unwrap_or(default)
        .to_string()
}

/// `config_get_bool`: "true"/"1" or "false"/"0", anything else (or missing) keeps `default`.
fn cfg_bool(text: &str, key: &str, default: bool) -> bool {
    match cfg_value(text, key) {
        Some("true" | "1") => true,
        Some("false" | "0") => false,
        _ => default,
    }
}

/// Saves go next to the content only with `savefiles_in_content_dir = "true"`; otherwise into
/// `savefile_directory`, falling back to the platform default dir. Missing flags take the
/// `config.def.h` defaults: per-core sorting on, per-content sorting and content-dir saves off.
pub fn parse_save_layout(cfg_text: &str, platform: RetroArchPlatform) -> SaveLayout {
    let d = platform.defaults();
    let root = if cfg_bool(cfg_text, "savefiles_in_content_dir", false) {
        SaveRoot::ContentDir
    } else {
        SaveRoot::Dir(cfg_dir(cfg_text, "savefile_directory", d.save_dir))
    };
    SaveLayout {
        root,
        sort_by_core: cfg_bool(cfg_text, "sort_savefiles_enable", true),
        sort_by_content: cfg_bool(cfg_text, "sort_savefiles_by_content_enable", false),
        cores_dir: cfg_dir(cfg_text, "libretro_directory", d.cores_dir),
        info_dir: cfg_dir(cfg_text, "libretro_info_path", d.info_dir),
    }
}

pub fn parse_core_info(info_text: &str) -> Option<CoreInfo> {
    let library_name = cfg_value(info_text, "corename")?.to_string();
    let extensions = cfg_value(info_text, "supported_extensions")?
        .split('|')
        .map(|e| e.trim().to_ascii_lowercase())
        .filter(|e| !e.is_empty())
        .collect();
    Some(CoreInfo {
        library_name,
        extensions,
    })
}

fn preferred_cores(system: SystemId) -> &'static [&'static str] {
    match system {
        SystemId::Nds => &["melonDS", "melonDS DS", "DeSmuME"],
        SystemId::Gba => &["mGBA", "gpSP", "VBA-M"],
        SystemId::Gb => &["Gambatte", "SameBoy", "Gearboy", "mGBA"],
        SystemId::Nes => &["Nestopia", "Mesen", "FCEUmm", "QuickNES"],
        SystemId::Snes => &["Snes9x", "bsnes"],
        SystemId::Md => &["Genesis Plus GX", "PicoDrive"],
        SystemId::Sms | SystemId::Gg => &["Genesis Plus GX", "PicoDrive", "Gearsystem"],
        SystemId::Ngp => &["Beetle NeoPop", "RACE"],
        SystemId::N64 => &["Mupen64Plus-Next", "ParaLLEl N64"],
        SystemId::Psx => &["Beetle PSX", "PCSX-ReARMed", "SwanStation"],
        _ => &[],
    }
}

fn runs(core: &CoreInfo, exts: &[&str]) -> bool {
    core.extensions
        .iter()
        .any(|e| exts.iter().any(|x| x.eq_ignore_ascii_case(e)))
}

/// Installed cores that run `system`, preferred ones first, then alphabetically.
pub fn ranked_cores(system: SystemId, installed: &[CoreInfo]) -> Vec<&str> {
    let Some(rs) = rom_system(system) else {
        return Vec::new();
    };
    let prefs = preferred_cores(system);
    let rank = |name: &str| {
        prefs
            .iter()
            .position(|p| p.eq_ignore_ascii_case(name))
            .unwrap_or(prefs.len())
    };
    let mut cores: Vec<&str> = installed
        .iter()
        .filter(|c| runs(c, rs.rom_exts))
        .map(|c| c.library_name.as_str())
        .collect();
    cores.sort_by(|a, b| rank(a).cmp(&rank(b)).then_with(|| a.cmp(b)));
    cores.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    cores
}

/// True when `folder` is named after installed cores none of which runs `system`.
pub fn is_foreign_core_folder(system: SystemId, installed: &[CoreInfo], folder: &str) -> bool {
    let Some(rs) = rom_system(system) else {
        return false;
    };
    let mut named = installed
        .iter()
        .filter(|c| c.library_name.eq_ignore_ascii_case(folder))
        .peekable();
    named.peek().is_some() && !named.any(|c| runs(c, rs.rom_exts))
}

/// The per-core save sub-folder RetroArch uses for `system`: an installed core whose folder
/// already exists (on-disk spelling), else the preferred installed core, else the alphabetically
/// first one.
pub fn core_save_folder(
    system: SystemId,
    installed: &[CoreInfo],
    existing_subdirs: &[String],
) -> Option<String> {
    let candidates = ranked_cores(system, installed);
    candidates
        .iter()
        .find_map(|c| {
            existing_subdirs
                .iter()
                .find(|d| d.eq_ignore_ascii_case(c))
                .cloned()
        })
        .or_else(|| candidates.first().map(|c| c.to_string()))
}

fn push_dir(base: &mut String, name: &str) {
    if name.is_empty() {
        return;
    }
    if !base.is_empty() {
        base.push('/');
    }
    base.push_str(name);
}

/// `base` + `/<content dir name>` + `/<core>` per the sort flags: where RetroArch keeps
/// `rom_path`'s save under that save root (runloop.c `runloop_path_set_redirect`, which also
/// sorts a content-dir root and never sorts an empty one).
pub fn sorted_save_dir(
    layout: &SaveLayout,
    base: &str,
    core_folder: Option<&str>,
    rom_path: &str,
) -> String {
    let mut out = base.to_string();
    if out.is_empty() {
        return out;
    }
    if layout.sort_by_content {
        push_dir(&mut out, base_name(parent_dir(rom_path)));
    }
    if layout.sort_by_core
        && let Some(core) = core_folder
    {
        push_dir(&mut out, core);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const USER_CFG: &str = r#"# RetroArch config
savefile_directory = "/retroarch/cores/savefiles"
savefiles_in_content_dir = "false"
sort_savefiles_enable = "true"
sort_savefiles_by_content_enable = "false"
libretro_directory = "/retroarch/cores"
garbage line without equals
"#;

    const MELONDS_INFO: &str = r#"# Software Information
display_name = "Nintendo - DS (melonDS)"
supported_extensions = "nds|ids|dsi"
corename = "melonDS"
"#;

    fn dir(s: &str) -> SaveRoot {
        SaveRoot::Dir(s.to_string())
    }

    fn core(name: &str, exts: &[&str]) -> CoreInfo {
        CoreInfo {
            library_name: name.to_string(),
            extensions: exts.iter().map(|e| e.to_string()).collect(),
        }
    }

    fn layout(root: SaveRoot, by_core: bool, by_content: bool) -> SaveLayout {
        SaveLayout {
            root,
            sort_by_core: by_core,
            sort_by_content: by_content,
            cores_dir: "retroarch/cores".to_string(),
            info_dir: "retroarch/info".to_string(),
        }
    }

    const SW: RetroArchPlatform = RetroArchPlatform::Switch;
    const CTR: RetroArchPlatform = RetroArchPlatform::Ctr;

    #[test]
    fn parses_the_user_switch_cfg() {
        let l = parse_save_layout(USER_CFG, SW);
        assert_eq!(l, layout(dir("retroarch/cores/savefiles"), true, false));
    }

    #[test]
    fn platform_parse_and_defaults() {
        assert_eq!(RetroArchPlatform::parse("switch"), Some(SW));
        assert_eq!(RetroArchPlatform::parse("3ds"), Some(CTR));
        assert_eq!(RetroArchPlatform::parse("wii"), None);
        assert_eq!(SW.defaults(), SWITCH_DEFAULTS);
        assert_eq!(CTR.defaults(), CTR_DEFAULTS);
    }

    #[test]
    fn default_or_missing_save_dir_is_the_platform_default() {
        for platform in [SW, CTR] {
            for cfg in [
                "savefile_directory = \"default\"",
                "savefile_directory = \"\"",
                "savefile_directory = \"/\"",
                "sort_savefiles_enable = \"true\"",
                "",
            ] {
                assert_eq!(
                    parse_save_layout(cfg, platform).root,
                    dir("retroarch/cores/savefiles"),
                    "{cfg}"
                );
            }
        }
    }

    #[test]
    fn content_dir_only_when_enabled() {
        for platform in [SW, CTR] {
            assert_eq!(
                parse_save_layout(
                    "savefile_directory = \"/x\"\nsavefiles_in_content_dir = \"true\"",
                    platform
                )
                .root,
                SaveRoot::ContentDir
            );
        }
    }

    #[test]
    fn info_dir_default_differs_per_platform() {
        let sw = parse_save_layout("", SW);
        assert_eq!(
            (sw.cores_dir.as_str(), sw.info_dir.as_str()),
            ("retroarch/cores", "retroarch/info")
        );
        let ctr = parse_save_layout("", CTR);
        assert_eq!(ctr.root, dir("retroarch/cores/savefiles"));
        assert_eq!(
            (ctr.cores_dir.as_str(), ctr.info_dir.as_str()),
            ("retroarch/cores", "retroarch/cores/info")
        );
        let def = parse_save_layout(
            "libretro_directory = \"default\"\nlibretro_info_path = \"default\"",
            CTR,
        );
        assert_eq!(def.info_dir, "retroarch/cores/info");
    }

    #[test]
    fn save_root_is_normalized() {
        assert_eq!(
            parse_save_layout("savefile_directory = \"sdmc:/x/\"", SW).root,
            dir("x")
        );
        assert_eq!(
            parse_save_layout("savefile_directory = /x", SW).root,
            dir("x")
        );
    }

    #[test]
    fn comments_and_dirs_defaults() {
        let l = parse_save_layout(
            "# savefile_directory = \"/nope\"\nlibretro_directory = \"default\"\nlibretro_info_path = \"sdmc:/ra/info/\"\n",
            SW,
        );
        assert_eq!(l.root, dir("retroarch/cores/savefiles"));
        assert_eq!(l.cores_dir, "retroarch/cores");
        assert_eq!(l.info_dir, "ra/info");
    }

    #[test]
    fn missing_sort_flags_take_retroarch_defaults() {
        let l = parse_save_layout("", SW);
        assert!(l.sort_by_core && !l.sort_by_content);
        let l = parse_save_layout(
            "sort_savefiles_enable = \"false\"\nsort_savefiles_by_content_enable = \"1\"",
            CTR,
        );
        assert!(!l.sort_by_core && l.sort_by_content);
        let l = parse_save_layout(
            "sort_savefiles_enable = \"0\"\nsavefiles_in_content_dir = \"1\"",
            SW,
        );
        assert!(!l.sort_by_core);
        assert_eq!(l.root, SaveRoot::ContentDir);
        assert!(parse_save_layout("sort_savefiles_enable = \"maybe\"", SW).sort_by_core);
    }

    #[test]
    fn parses_melonds_info() {
        assert_eq!(
            parse_core_info(MELONDS_INFO),
            Some(core("melonDS", &["nds", "ids", "dsi"]))
        );
        assert_eq!(
            parse_core_info("supported_extensions = \"NDS\""),
            None,
            "missing corename"
        );
        assert_eq!(parse_core_info("corename = \"x\""), None);
    }

    #[test]
    fn core_folder_prefers_known_core() {
        let installed = [core("mGBA", &["gba", "gb"]), core("melonDS", &["nds"])];
        assert_eq!(
            core_save_folder(SystemId::Gba, &installed, &[]),
            Some("mGBA".to_string())
        );
    }

    #[test]
    fn core_folder_existing_subdir_wins() {
        let installed = [core("gpSP", &["gba"]), core("mGBA", &["gba"])];
        assert_eq!(
            core_save_folder(SystemId::Gba, &installed, &["gpsp".to_string()]),
            Some("gpsp".to_string())
        );
        assert_eq!(
            core_save_folder(SystemId::Gba, &installed, &[]),
            Some("mGBA".to_string())
        );
    }

    #[test]
    fn core_folder_unknown_cores_alphabetical_and_none() {
        let installed = [core("Zeta", &["gba"]), core("Alpha", &["gba"])];
        assert_eq!(
            core_save_folder(SystemId::Gba, &installed, &[]),
            Some("Alpha".to_string())
        );
        assert_eq!(
            core_save_folder(SystemId::Nds, &[core("mGBA", &["gba"])], &[]),
            None
        );
    }

    #[test]
    fn ranked_and_foreign_cores() {
        let installed = [
            core("mGBA", &["gba", "gb"]),
            core("Nestopia", &["nes"]),
            core("gpSP", &["gba"]),
        ];
        assert_eq!(ranked_cores(SystemId::Gba, &installed), ["mGBA", "gpSP"]);
        assert!(is_foreign_core_folder(
            SystemId::Gba,
            &installed,
            "nestopia"
        ));
        assert!(!is_foreign_core_folder(SystemId::Gba, &installed, "MGBA"));
        assert!(!is_foreign_core_folder(SystemId::Gba, &installed, "gba"));
    }

    #[test]
    fn sorted_dir_is_content_then_core() {
        let rom = "roms/gba/Zelda.gba";
        let base = "ra/saves";
        let sorted = |by_core, by_content, core| {
            sorted_save_dir(&layout(dir(base), by_core, by_content), base, core, rom)
        };
        assert_eq!(sorted(false, false, Some("mGBA")), "ra/saves");
        assert_eq!(sorted(true, false, Some("mGBA")), "ra/saves/mGBA");
        assert_eq!(sorted(false, true, Some("mGBA")), "ra/saves/gba");
        assert_eq!(sorted(true, true, Some("mGBA")), "ra/saves/gba/mGBA");
        assert_eq!(sorted(true, true, None), "ra/saves/gba");
        let content = layout(SaveRoot::ContentDir, true, true);
        assert_eq!(
            sorted_save_dir(&content, "roms/gba", Some("mGBA"), rom),
            "roms/gba/gba/mGBA"
        );
        assert_eq!(sorted_save_dir(&content, "", Some("mGBA"), "X.gba"), "");
    }
}
