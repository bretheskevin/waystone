use serde::{Deserialize, Serialize};
use waystone_core::conflict::ConflictPolicy;
use anyhow::{Context, Result};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WaystoneConfig {
    pub device_id: String,
    pub server_url: String,
    pub conflict_policy: ConflictPolicy,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub username: Option<String>,
}

impl Default for WaystoneConfig {
    fn default() -> Self {
        Self {
            device_id: uuid::Uuid::new_v4().to_string(),
            server_url: String::new(),
            conflict_policy: ConflictPolicy::NewestWins,
            username: None,
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
        let cfg: Self = serde_json::from_str(&data)
            .with_context(|| format!("parsing {}", path.display()))?;
        Ok(cfg)
    }

    pub fn save(&self) -> Result<()> {
        let dir = Self::config_dir()?;
        std::fs::create_dir_all(&dir)?;
        let path = dir.join("config.json");
        let data = serde_json::to_string_pretty(self)?;
        std::fs::write(&path, data)?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_config_has_sane_values() {
        let cfg = WaystoneConfig::default();
        assert!(!cfg.device_id.is_empty());
        assert_eq!(cfg.conflict_policy, waystone_core::conflict::ConflictPolicy::NewestWins);
        assert!(cfg.username.is_none());
    }

    #[test]
    fn config_round_trips_to_json() {
        let cfg = WaystoneConfig {
            device_id: "test-device-123".into(),
            server_url: "https://dav.example.com/waystone".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: Some("alice".into()),
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
        };
        let json = serde_json::to_string_pretty(&cfg).unwrap();
        assert!(!json.contains("username"));
    }
}
