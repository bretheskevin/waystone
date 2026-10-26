use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use waystone_core::conflict::ConflictPolicy;
use waystone_core::rom_flavor::SaveFlavor;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SyncTarget {
    pub name: String,
    pub path: PathBuf,
    pub adapter: String,
    pub system: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub save_dir: Option<PathBuf>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub preset: Option<String>,
}

impl SyncTarget {
    pub fn plain(name: &str, path: &std::path::Path, adapter: &str, system: &str) -> Self {
        Self {
            name: name.into(),
            path: path.to_path_buf(),
            adapter: adapter.into(),
            system: system.into(),
            save_dir: None,
            preset: None,
        }
    }

    /// Mirrors Android `SourceFolder.externalFlavor()`: None without a save dir.
    pub fn external_flavor(&self) -> Option<SaveFlavor> {
        self.save_dir.as_ref()?;
        Some(
            self.preset
                .as_deref()
                .and_then(crate::presets::preset)
                .map_or(SaveFlavor::Native, |p| p.flavor),
        )
    }
}

const KNOWN_ADAPTERS: &[&str] = &["jksv", "mgba", "rom_keyed", "twilight", "checkpoint"];
const KNOWN_SYSTEMS: &[&str] = &[
    "switch", "3ds", "nds", "gba", "gbc", "gb", "nes", "snes", "md", "sms", "gg", "ngp",
];

pub fn validate_sync_target(t: &SyncTarget) -> Result<()> {
    if t.name.trim().is_empty() {
        anyhow::bail!("target name must not be empty");
    }
    if !KNOWN_ADAPTERS.contains(&t.adapter.as_str()) {
        anyhow::bail!(
            "unknown adapter '{}'; expected one of: {}",
            t.adapter,
            KNOWN_ADAPTERS.join(", ")
        );
    }
    if !crate::helpers::is_rom_keyed(&t.adapter) && !KNOWN_SYSTEMS.contains(&t.system.as_str()) {
        anyhow::bail!(
            "unknown system '{}'; expected one of: {}",
            t.system,
            KNOWN_SYSTEMS.join(", ")
        );
    }
    if let Some(id) = t.preset.as_deref() {
        let p =
            crate::presets::preset(id).ok_or_else(|| anyhow::anyhow!("unknown preset '{id}'"))?;
        if !crate::helpers::is_rom_keyed(&t.adapter) {
            anyhow::bail!("preset '{id}' requires a rom_keyed adapter");
        }
        if p.needs_save_dir && t.save_dir.is_none() {
            anyhow::bail!("preset '{id}' requires a save directory");
        }
    }
    Ok(())
}

fn default_true() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WaystoneConfig {
    pub device_id: String,
    pub server_url: String,
    pub conflict_policy: ConflictPolicy,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub username: Option<String>,
    #[serde(default)]
    pub targets: Vec<SyncTarget>,
    #[serde(default = "default_true")]
    pub safety_backup: bool,
}

impl Default for WaystoneConfig {
    fn default() -> Self {
        Self {
            device_id: uuid::Uuid::new_v4().to_string(),
            server_url: String::new(),
            conflict_policy: ConflictPolicy::NewestWins,
            username: None,
            targets: Vec::new(),
            safety_backup: true,
        }
    }
}

impl WaystoneConfig {
    pub fn config_dir() -> Result<PathBuf> {
        let dir = dirs::config_dir()
            .context("could not determine config directory")?
            .join("waystone");
        Ok(dir)
    }

    /// The configured target for the same path + adapter (+ system for folder adapters, so an
    /// explicit `--system` is never overridden), letting presets/save dir apply to CLI paths;
    /// else `plain` unchanged.
    pub fn resolve_target(&self, plain: SyncTarget) -> SyncTarget {
        let same_path = |a: &std::path::Path, b: &std::path::Path| {
            a == b || matches!((a.canonicalize(), b.canonicalize()), (Ok(x), Ok(y)) if x == y)
        };
        let rom = crate::helpers::is_rom_keyed(&plain.adapter);
        match self.targets.iter().find(|t| {
            crate::helpers::is_rom_keyed(&t.adapter) == rom
                && (rom || (t.adapter == plain.adapter && t.system == plain.system))
                && same_path(&t.path, &plain.path)
        }) {
            Some(t) => {
                log::info!(
                    "[sync] CLI path {} matches configured target '{}'",
                    plain.path.display(),
                    t.name
                );
                t.clone()
            }
            None => plain,
        }
    }

    pub fn load() -> Result<Self> {
        let path = Self::config_dir()?.join("config.json");
        if !path.exists() {
            return Ok(Self::default());
        }
        let data = std::fs::read_to_string(&path)
            .with_context(|| format!("reading {}", path.display()))?;
        let cfg: Self =
            serde_json::from_str(&data).with_context(|| format!("parsing {}", path.display()))?;
        Ok(cfg)
    }

    pub fn save(&self) -> Result<()> {
        self.save_to(&Self::config_dir()?.join("config.json"))
    }

    pub fn save_to(&self, path: &std::path::Path) -> Result<()> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let data = serde_json::to_string_pretty(self)?;
        std::fs::write(path, data)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rom_target(preset: Option<&str>, save_dir: Option<&str>) -> SyncTarget {
        SyncTarget {
            save_dir: save_dir.map(PathBuf::from),
            preset: preset.map(String::from),
            ..SyncTarget::plain("Roms", std::path::Path::new("/roms"), "rom_keyed", "")
        }
    }

    #[test]
    fn old_target_json_without_new_fields_parses() {
        let json = r#"{"name":"n","path":"/p","adapter":"jksv","system":"switch"}"#;
        let t: SyncTarget = serde_json::from_str(json).unwrap();
        assert_eq!(t.save_dir, None);
        assert_eq!(t.preset, None);
        assert!(!serde_json::to_string(&t).unwrap().contains("save_dir"));
    }

    #[test]
    fn retroarch_preset_requires_save_dir() {
        assert!(validate_sync_target(&rom_target(Some("retroarch"), None)).is_err());
        validate_sync_target(&rom_target(Some("retroarch"), Some("/saves"))).unwrap();
        validate_sync_target(&rom_target(Some("melonds"), None)).unwrap();
    }

    #[test]
    fn unknown_preset_or_non_rom_keyed_preset_rejected() {
        assert!(validate_sync_target(&rom_target(Some("nope"), None)).is_err());
        let t = SyncTarget {
            preset: Some("melonds".into()),
            ..SyncTarget::plain("x", std::path::Path::new("/x"), "jksv", "switch")
        };
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn external_flavor_matrix() {
        assert_eq!(rom_target(None, None).external_flavor(), None);
        assert_eq!(
            rom_target(Some("retroarch"), Some("/s")).external_flavor(),
            Some(SaveFlavor::RetroArch)
        );
        assert_eq!(
            rom_target(Some("melonds"), Some("/s")).external_flavor(),
            Some(SaveFlavor::Native)
        );
        assert_eq!(
            rom_target(None, Some("/s")).external_flavor(),
            Some(SaveFlavor::Native)
        );
    }

    #[test]
    fn rom_keyed_targets_ignore_system() {
        for adapter in ["rom_keyed", "twilight"] {
            let t = SyncTarget::plain("Roms", std::path::Path::new("/roms"), adapter, "");
            validate_sync_target(&t).unwrap();
        }
        let bad = SyncTarget::plain("x", std::path::Path::new("/x"), "jksv", "");
        assert!(validate_sync_target(&bad).is_err());
    }

    #[test]
    fn sync_target_round_trips_to_json() {
        let target = SyncTarget::plain(
            "Switch JKSV",
            std::path::Path::new("/mnt/sd/JKSV"),
            "jksv",
            "switch",
        );
        let json = serde_json::to_string(&target).unwrap();
        let parsed: SyncTarget = serde_json::from_str(&json).unwrap();
        assert_eq!(target, parsed);
    }

    #[test]
    fn config_without_targets_field_loads_with_empty_vec() {
        let json =
            r#"{"device_id":"dev","server_url":"http://srv","conflict_policy":"newest-wins"}"#;
        let cfg: WaystoneConfig = serde_json::from_str(json).unwrap();
        assert!(cfg.targets.is_empty());
    }

    #[test]
    fn config_with_targets_round_trips() {
        let cfg = WaystoneConfig {
            device_id: "dev".into(),
            server_url: "http://srv".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: None,
            targets: vec![SyncTarget::plain(
                "My Switch",
                std::path::Path::new("/saves"),
                "jksv",
                "switch",
            )],
            safety_backup: true,
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        let cfg2: WaystoneConfig = serde_json::from_str(&json).unwrap();
        assert_eq!(cfg.targets.len(), cfg2.targets.len());
        assert_eq!(cfg.targets[0].name, cfg2.targets[0].name);
    }

    #[test]
    fn validate_sync_target_rejects_empty_name() {
        let t = SyncTarget::plain("", std::path::Path::new("/saves"), "jksv", "switch");
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_rejects_unknown_adapter() {
        let t = SyncTarget::plain("test", std::path::Path::new("/saves"), "unknown", "switch");
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_rejects_unknown_system() {
        let t = SyncTarget::plain("test", std::path::Path::new("/saves"), "jksv", "ps5");
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_accepts_valid_target() {
        let t = SyncTarget::plain(
            "Switch JKSV",
            std::path::Path::new("/saves"),
            "jksv",
            "switch",
        );
        assert!(validate_sync_target(&t).is_ok());
    }

    #[test]
    fn default_config_has_sane_values() {
        let cfg = WaystoneConfig::default();
        assert!(!cfg.device_id.is_empty());
        assert_eq!(
            cfg.conflict_policy,
            waystone_core::conflict::ConflictPolicy::NewestWins
        );
        assert!(cfg.username.is_none());
        assert!(cfg.safety_backup);
    }

    #[test]
    fn config_round_trips_to_json() {
        let cfg = WaystoneConfig {
            device_id: "test-device-123".into(),
            server_url: "https://dav.example.com/waystone".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: Some("alice".into()),
            targets: vec![],
            safety_backup: true,
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        let cfg2: WaystoneConfig = serde_json::from_str(&json).unwrap();
        assert_eq!(cfg.device_id, cfg2.device_id);
        assert_eq!(cfg.server_url, cfg2.server_url);
        assert_eq!(cfg.username, cfg2.username);
    }

    #[test]
    fn config_without_username_omits_field() {
        let cfg = WaystoneConfig {
            device_id: "dev".into(),
            server_url: "http://srv".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: None,
            targets: vec![],
            safety_backup: true,
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        assert!(!json.contains("username"));
    }

    #[test]
    fn config_without_safety_backup_deserializes_as_true() {
        let json =
            r#"{"device_id":"dev","server_url":"http://srv","conflict_policy":"newest-wins"}"#;
        let cfg: WaystoneConfig = serde_json::from_str(json).unwrap();
        assert!(cfg.safety_backup);
    }

    #[test]
    fn config_with_safety_backup_false_round_trips() {
        let cfg = WaystoneConfig {
            safety_backup: false,
            ..Default::default()
        };
        let json = serde_json::to_string(&cfg).unwrap();
        let cfg2: WaystoneConfig = serde_json::from_str(&json).unwrap();
        assert!(!cfg2.safety_backup);
    }

    #[test]
    fn resolve_target_picks_configured_flavored_target_for_same_path() {
        let configured = SyncTarget {
            save_dir: Some("/ra".into()),
            preset: Some("retroarch".into()),
            ..SyncTarget::plain("Roms", std::path::Path::new("/roms"), "rom_keyed", "")
        };
        let cfg = WaystoneConfig {
            targets: vec![configured.clone()],
            ..Default::default()
        };
        let hit = cfg.resolve_target(SyncTarget::plain(
            "cli",
            std::path::Path::new("/roms"),
            "twilight",
            "",
        ));
        assert_eq!(hit, configured);
        let miss = SyncTarget::plain("cli", std::path::Path::new("/other"), "rom_keyed", "");
        assert_eq!(cfg.resolve_target(miss.clone()), miss);
        let other_adapter = SyncTarget::plain("cli", std::path::Path::new("/roms"), "mgba", "gba");
        assert_eq!(cfg.resolve_target(other_adapter.clone()), other_adapter);
    }

    #[test]
    fn resolve_target_keeps_an_explicit_system_for_folder_adapters() {
        let configured = SyncTarget::plain("GBA", std::path::Path::new("/saves"), "mgba", "gba");
        let cfg = WaystoneConfig {
            targets: vec![configured.clone()],
            ..Default::default()
        };
        let same = SyncTarget::plain("cli", std::path::Path::new("/saves"), "mgba", "gba");
        assert_eq!(cfg.resolve_target(same), configured);
        let gbc = SyncTarget::plain("cli", std::path::Path::new("/saves"), "mgba", "gbc");
        assert_eq!(cfg.resolve_target(gbc.clone()), gbc);
    }
}
