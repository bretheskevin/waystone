use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use waystone_core::conflict::ConflictPolicy;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SyncTarget {
    pub name: String,
    pub path: PathBuf,
    pub adapter: String,
    pub system: String,
}

const KNOWN_ADAPTERS: &[&str] = &["jksv", "mgba", "twilight", "checkpoint"];
const KNOWN_SYSTEMS: &[&str] = &["switch", "3ds", "nds", "gba", "gbc", "gb"];

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
    if !KNOWN_SYSTEMS.contains(&t.system.as_str()) {
        anyhow::bail!(
            "unknown system '{}'; expected one of: {}",
            t.system,
            KNOWN_SYSTEMS.join(", ")
        );
    }
    Ok(())
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
}

impl Default for WaystoneConfig {
    fn default() -> Self {
        Self {
            device_id: uuid::Uuid::new_v4().to_string(),
            server_url: String::new(),
            conflict_policy: ConflictPolicy::NewestWins,
            username: None,
            targets: Vec::new(),
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

    #[test]
    fn sync_target_round_trips_to_json() {
        let target = SyncTarget {
            name: "Switch JKSV".into(),
            path: PathBuf::from("/mnt/sd/JKSV"),
            adapter: "jksv".into(),
            system: "switch".into(),
        };
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
            targets: vec![SyncTarget {
                name: "My Switch".into(),
                path: PathBuf::from("/saves"),
                adapter: "jksv".into(),
                system: "switch".into(),
            }],
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        let cfg2: WaystoneConfig = serde_json::from_str(&json).unwrap();
        assert_eq!(cfg.targets.len(), cfg2.targets.len());
        assert_eq!(cfg.targets[0].name, cfg2.targets[0].name);
    }

    #[test]
    fn validate_sync_target_rejects_empty_name() {
        let t = SyncTarget {
            name: "".into(),
            path: PathBuf::from("/saves"),
            adapter: "jksv".into(),
            system: "switch".into(),
        };
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_rejects_unknown_adapter() {
        let t = SyncTarget {
            name: "test".into(),
            path: PathBuf::from("/saves"),
            adapter: "unknown".into(),
            system: "switch".into(),
        };
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_rejects_unknown_system() {
        let t = SyncTarget {
            name: "test".into(),
            path: PathBuf::from("/saves"),
            adapter: "jksv".into(),
            system: "ps5".into(),
        };
        assert!(validate_sync_target(&t).is_err());
    }

    #[test]
    fn validate_sync_target_accepts_valid_target() {
        let t = SyncTarget {
            name: "Switch JKSV".into(),
            path: PathBuf::from("/saves"),
            adapter: "jksv".into(),
            system: "switch".into(),
        };
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
    }

    #[test]
    fn config_round_trips_to_json() {
        let cfg = WaystoneConfig {
            device_id: "test-device-123".into(),
            server_url: "https://dav.example.com/waystone".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: Some("alice".into()),
            targets: vec![],
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
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        assert!(!json.contains("username"));
    }
}
