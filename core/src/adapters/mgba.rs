use crate::adapters::Adapter;
use crate::model::*;

pub struct MgbaAdapter {
    system: SystemId,
}

impl MgbaAdapter {
    pub fn new(system: SystemId) -> Self {
        Self { system }
    }

    fn parse_filename(path: &str) -> Option<(String, FileKind)> {
        let filename = path.rsplit('/').next().unwrap_or(path);
        if let Some(stem) = filename.strip_suffix(".sav") {
            return Some((stem.to_string(), FileKind::Battery));
        }
        if let Some(pos) = filename.rfind(".ss") {
            let after = &filename[pos + 3..];
            if !after.is_empty() && after.chars().all(|c| c.is_ascii_digit()) {
                let stem = &filename[..pos];
                return Some((stem.to_string(), FileKind::SaveState(after.to_string())));
            }
        }
        None
    }
}

enum FileKind {
    Battery,
    SaveState(String),
}

impl Adapter for MgbaAdapter {
    fn id(&self) -> &str {
        "mgba"
    }

    fn systems(&self) -> &[SystemId] {
        std::slice::from_ref(&self.system)
    }

    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave> {
        let mut saves = Vec::new();

        for file in &raw.files {
            let Some((stem, file_kind)) = Self::parse_filename(&file.path) else {
                continue;
            };

            let (kind, slot, portable, ext) = match file_kind {
                FileKind::Battery => (
                    SaveKind::Battery,
                    "battery".to_string(),
                    true,
                    "sav".to_string(),
                ),
                FileKind::SaveState(n) => {
                    let ext = format!("ss{}", n);
                    (SaveKind::SaveState, format!("state-{}", n), false, ext)
                }
            };

            saves.push(NormalizedSave {
                id: SaveId {
                    source: "mgba".into(),
                    system: self.system,
                    game: GameRef {
                        key: stem.clone(),
                        display_name: stem.clone(),
                        confidence: Confidence::Weak,
                        title_id: None,
                        serial: None,
                        rom_crc: None,
                    },
                    slot: slot.clone(),
                    kind,
                },
                group_key: build_group_key(self.system, &stem, &slot),
                portable,
                mtime: String::new(),
                files: vec![(format!("{}.{}", stem, ext), file.content.clone())],
            });
        }

        saves
    }

    fn to_native(&self, save: &NormalizedSave) -> RawTree {
        let ext = match save.id.kind {
            SaveKind::Battery => "sav".to_string(),
            SaveKind::SaveState => {
                let n = save.id.slot.strip_prefix("state-").unwrap_or("0");
                format!("ss{}", n)
            }
            SaveKind::Native => "bin".to_string(),
        };
        RawTree {
            files: save
                .files
                .iter()
                .map(|(_rel, content)| RawFile {
                    path: format!("{}.{}", save.id.game.display_name, ext),
                    content: content.clone(),
                })
                .collect(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{Confidence, RawFile, RawTree, SaveKind, SystemId};

    fn make_mgba_tree() -> RawTree {
        RawTree {
            files: vec![
                RawFile {
                    path: "Pokemon Emerald.sav".into(),
                    content: vec![0xFF; 128],
                },
                RawFile {
                    path: "Pokemon Emerald.ss0".into(),
                    content: vec![0xAA; 64],
                },
                RawFile {
                    path: "Metroid Fusion.sav".into(),
                    content: vec![0xBB; 256],
                },
            ],
        }
    }

    #[test]
    fn mgba_normalizes_battery_saves() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let batteries: Vec<_> = saves
            .iter()
            .filter(|s| s.id.kind == SaveKind::Battery)
            .collect();
        assert_eq!(batteries.len(), 2);
    }

    #[test]
    fn mgba_normalizes_save_states() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let states: Vec<_> = saves
            .iter()
            .filter(|s| s.id.kind == SaveKind::SaveState)
            .collect();
        assert_eq!(states.len(), 1);
    }

    #[test]
    fn mgba_battery_is_portable() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let battery = saves
            .iter()
            .find(|s| s.id.kind == SaveKind::Battery && s.id.game.display_name == "Pokemon Emerald")
            .unwrap();
        assert!(battery.portable);
    }

    #[test]
    fn mgba_savestate_is_not_portable() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let state = saves
            .iter()
            .find(|s| s.id.kind == SaveKind::SaveState)
            .unwrap();
        assert!(!state.portable);
    }

    #[test]
    fn mgba_game_key_from_filename() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let emerald = saves
            .iter()
            .find(|s| s.id.game.display_name == "Pokemon Emerald" && s.id.kind == SaveKind::Battery)
            .unwrap();
        assert_eq!(emerald.id.game.key, "Pokemon Emerald");
        assert_eq!(emerald.id.game.confidence, Confidence::Weak);
    }

    #[test]
    fn mgba_to_native_round_trips_battery() {
        let adapter = MgbaAdapter::new(SystemId::Gba);
        let saves = adapter.normalize(&make_mgba_tree());
        let battery = saves
            .iter()
            .find(|s| s.id.kind == SaveKind::Battery && s.id.game.display_name == "Pokemon Emerald")
            .unwrap();
        let native = adapter.to_native(battery);
        assert_eq!(native.files.len(), 1);
        assert_eq!(native.files[0].path, "Pokemon Emerald.sav");
        assert_eq!(native.files[0].content, vec![0xFF; 128]);
    }
}
