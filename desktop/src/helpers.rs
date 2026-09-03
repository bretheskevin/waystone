use anyhow::Result;
use std::path::{Path, PathBuf};
use waystone_core::adapters::Adapter;
use waystone_core::model::{RawFile, RawTree, SystemId};

pub fn parse_system(s: &str) -> Result<SystemId> {
    match s {
        "switch" => Ok(SystemId::Switch),
        "3ds" => Ok(SystemId::ThreeDS),
        "nds" => Ok(SystemId::Nds),
        "gba" => Ok(SystemId::Gba),
        "gbc" => Ok(SystemId::Gbc),
        "gb" => Ok(SystemId::Gb),
        other => anyhow::bail!("unknown system: {}", other),
    }
}

pub fn make_adapter(name: &str, system: SystemId) -> Result<Box<dyn Adapter>> {
    match name {
        "jksv" => Ok(Box::new(waystone_core::adapters::jksv::JksvAdapter::new(
            system,
        ))),
        "mgba" => Ok(Box::new(waystone_core::adapters::mgba::MgbaAdapter::new(
            system,
        ))),
        "twilight" => Ok(Box::new(
            waystone_core::adapters::twilight::TwilightAdapter::new(),
        )),
        "checkpoint" => Ok(Box::new(
            waystone_core::adapters::checkpoint::CheckpointAdapter::new(system),
        )),
        other => anyhow::bail!("unknown adapter: {}", other),
    }
}

pub fn read_source_tree(path: &Path) -> Result<RawTree> {
    let mut files = Vec::new();
    for entry in walkdir(path)? {
        let rel = entry.strip_prefix(path)?.to_string_lossy().to_string();
        let content = std::fs::read(&entry)?;
        files.push(RawFile { path: rel, content });
    }
    Ok(RawTree { files })
}

pub fn walkdir(dir: &Path) -> Result<Vec<PathBuf>> {
    let mut result = Vec::new();
    if dir.is_dir() {
        for entry in std::fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.is_dir() {
                result.extend(walkdir(&path)?);
            } else {
                result.push(path);
            }
        }
    }
    Ok(result)
}
