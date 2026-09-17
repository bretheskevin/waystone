use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::adapters::Adapter;
use crate::adapters::folder_layout::{folder_layout_to_native, normalize_folder_layout};
use crate::model::*;

pub struct CheckpointAdapter {
    system: SystemId,
}

impl CheckpointAdapter {
    pub fn new(system: SystemId) -> Self {
        Self { system }
    }

    fn parse_title_dir(dir_name: &str) -> (String, Option<String>) {
        if let Some(rest) = dir_name.strip_prefix("0x") {
            if let Some(space_idx) = rest.find(' ') {
                let tid = &rest[..space_idx];
                if !tid.is_empty() {
                    let name = rest[space_idx + 1..].to_string();
                    let display = if name.is_empty() {
                        tid.to_string()
                    } else {
                        name
                    };
                    return (display, Some(tid.to_string()));
                }
            } else if !rest.is_empty() {
                let tid = rest.to_string();
                return (tid.clone(), Some(tid));
            }
        }
        (dir_name.to_string(), None)
    }
}

impl Adapter for CheckpointAdapter {
    fn id(&self) -> &str {
        "checkpoint"
    }

    fn systems(&self) -> &[SystemId] {
        core::slice::from_ref(&self.system)
    }

    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave> {
        normalize_folder_layout(raw, self.system, "checkpoint", Self::parse_title_dir)
    }

    fn to_native(&self, save: &NormalizedSave) -> RawTree {
        folder_layout_to_native(save, |name, tid| match tid {
            Some(tid) => format!("0x{} {}", tid, name),
            None => name.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::adapters::jksv::JksvAdapter;
    use crate::model::{Confidence, RawFile, RawTree, SaveKind, SystemId};

    fn make_checkpoint_tree() -> RawTree {
        RawTree {
            files: vec![
                RawFile {
                    path: "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/data.bin"
                        .into(),
                    content: vec![0xDE, 0xAD],
                },
                RawFile {
                    path: "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/extra.bin"
                        .into(),
                    content: vec![0xBE, 0xEF],
                },
                RawFile {
                    path: "0x0100000000010000 Super Mario Odyssey/slot1/save.dat".into(),
                    content: vec![1, 2, 3],
                },
            ],
        }
    }

    #[test]
    fn checkpoint_normalizes_switch_saves() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        assert_eq!(saves.len(), 2);
    }

    #[test]
    fn checkpoint_extracts_switch_title_id() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        let smash = saves
            .iter()
            .find(|s| s.id.game.key == "supersmashbrosultimate")
            .unwrap();
        assert_eq!(smash.id.game.display_name, "Super Smash Bros. Ultimate");
        assert_eq!(smash.id.game.title_id, Some("01006A800016E000".into()));
        assert_eq!(smash.id.game.confidence, Confidence::Strong);
    }

    #[test]
    fn checkpoint_switch_group_key_format() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        let smash = saves
            .iter()
            .find(|s| s.id.game.key == "supersmashbrosultimate")
            .unwrap();
        assert_eq!(smash.id.slot, "20230715-143052");
        assert_eq!(
            smash.group_key,
            "switch/supersmashbrosultimate/20230715-143052"
        );
    }

    #[test]
    fn checkpoint_groups_by_title_and_slot() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        let smash = saves
            .iter()
            .find(|s| s.id.game.key == "supersmashbrosultimate")
            .unwrap();
        assert_eq!(smash.files.len(), 2);
    }

    #[test]
    fn checkpoint_sets_kind_native() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        for save in &saves {
            assert_eq!(save.id.kind, SaveKind::Native);
        }
    }

    #[test]
    fn checkpoint_normalizes_3ds_saves() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x0055D Pokemon X/20230715-143052/main".into(),
                content: vec![0xFF; 64],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert_eq!(save.id.game.key, "pokemonx");
        assert_eq!(save.id.game.display_name, "Pokemon X");
        assert_eq!(save.id.game.title_id, Some("0055D".into()));
        assert_eq!(save.id.game.confidence, Confidence::Strong);
        assert_eq!(save.id.system, SystemId::ThreeDS);
        assert_eq!(save.group_key, "3ds/pokemonx/20230715-143052");
    }

    #[test]
    fn checkpoint_no_prefix_falls_back_to_weak_confidence() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let raw = RawTree {
            files: vec![RawFile {
                path: "My Custom Backup/20230715-143052/save.bin".into(),
                content: vec![0xAA],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert_eq!(save.id.game.key, "mycustombackup");
        assert_eq!(save.id.game.display_name, "My Custom Backup");
        assert!(save.id.game.title_id.is_none());
        assert_eq!(save.id.game.confidence, Confidence::Weak);
    }

    #[test]
    fn checkpoint_skips_short_paths() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let raw = RawTree {
            files: vec![
                RawFile {
                    path: "only_one_part".into(),
                    content: vec![],
                },
                RawFile {
                    path: "title/slot_only".into(),
                    content: vec![],
                },
                RawFile {
                    path: "0x0100000000010000 Super Mario Odyssey/slot1/save.dat".into(),
                    content: vec![1, 2, 3],
                },
            ],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
    }

    #[test]
    fn checkpoint_0x_no_space_falls_back_gracefully() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0xDEADBEEF/20230101-120000/save.bin".into(),
                content: vec![0x01],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert_eq!(save.id.game.title_id, Some("DEADBEEF".into()));
        assert_eq!(save.id.game.display_name, "DEADBEEF");
        assert_eq!(save.id.game.key, "deadbeef");
    }

    #[test]
    fn checkpoint_0x_empty_tid_falls_back_to_weak() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x Something/20230101-120000/save.bin".into(),
                content: vec![0x02],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert!(save.id.game.title_id.is_none());
        assert_eq!(save.id.game.confidence, Confidence::Weak);
        assert_eq!(save.id.game.display_name, "0x Something");
        assert_eq!(save.id.game.key, "0xsomething");
    }

    #[test]
    fn checkpoint_bare_0x_prefix_falls_back_to_weak() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x/20230101-120000/save.bin".into(),
                content: vec![0x03],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert!(save.id.game.title_id.is_none());
        assert_eq!(save.id.game.confidence, Confidence::Weak);
        assert_eq!(save.id.game.display_name, "0x");
    }

    #[test]
    fn checkpoint_to_native_round_trips_switch() {
        let adapter = CheckpointAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_checkpoint_tree());
        let smash = saves
            .iter()
            .find(|s| s.id.game.key == "supersmashbrosultimate")
            .unwrap();
        let native = adapter.to_native(smash);
        assert_eq!(native.files.len(), 2);
        let data_file = native
            .files
            .iter()
            .find(|f| f.path.ends_with("data.bin"))
            .unwrap();
        assert_eq!(
            data_file.path,
            "0x01006A800016E000 Super Smash Bros. Ultimate/20230715-143052/data.bin"
        );
        assert_eq!(data_file.content, vec![0xDE, 0xAD]);
    }

    #[test]
    fn checkpoint_to_native_round_trips_3ds() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x0055D Pokemon X/20230715-143052/main".into(),
                content: vec![0xFF; 64],
            }],
        };
        let saves = adapter.normalize(&raw);
        let native = adapter.to_native(&saves[0]);
        assert_eq!(native.files.len(), 1);
        assert_eq!(
            native.files[0].path,
            "0x0055D Pokemon X/20230715-143052/main"
        );
        assert_eq!(native.files[0].content, vec![0xFF; 64]);
    }

    #[test]
    fn checkpoint_normalizes_3ds_extdata_as_separate_save() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![
                RawFile {
                    path: "0x0055D Pokemon X/main/main".into(),
                    content: vec![0xFF; 64],
                },
                RawFile {
                    path: "0x0055D Pokemon X/extdata/00000001/00000002".into(),
                    content: vec![0xAA; 32],
                },
            ],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 2, "main + extdata must produce 2 saves");

        let main_save = saves.iter().find(|s| s.id.slot == "main").unwrap();
        assert_eq!(main_save.group_key, "3ds/pokemonx/main");
        assert_eq!(main_save.files.len(), 1);

        let ext_save = saves.iter().find(|s| s.id.slot == "extdata").unwrap();
        assert_eq!(ext_save.group_key, "3ds/pokemonx/extdata");
        assert_eq!(ext_save.files.len(), 1);
        assert_eq!(ext_save.files[0].0, "00000001/00000002");
        assert_eq!(ext_save.id.game.key, "pokemonx");
        assert_eq!(ext_save.id.game.title_id, Some("0055D".into()));
    }

    #[test]
    fn checkpoint_3ds_extdata_group_keys_are_distinct() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![
                RawFile {
                    path: "0x0055D Pokemon X/main/main".into(),
                    content: vec![0xFF; 64],
                },
                RawFile {
                    path: "0x0055D Pokemon X/extdata/boss/00000001".into(),
                    content: vec![0xBB; 16],
                },
            ],
        };
        let saves = adapter.normalize(&raw);
        let keys: Vec<&str> = saves.iter().map(|s| s.group_key.as_str()).collect();
        assert!(keys.contains(&"3ds/pokemonx/main"));
        assert!(keys.contains(&"3ds/pokemonx/extdata"));
        assert_ne!(keys[0], keys[1], "group_keys must differ");
    }

    #[test]
    fn checkpoint_3ds_extdata_to_native_round_trip() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x0055D Pokemon X/extdata/00000001/00000002".into(),
                content: vec![0xAA; 32],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let native = adapter.to_native(&saves[0]);
        assert_eq!(native.files.len(), 1);
        assert_eq!(
            native.files[0].path,
            "0x0055D Pokemon X/extdata/00000001/00000002"
        );
        assert_eq!(native.files[0].content, vec![0xAA; 32]);
    }

    #[test]
    fn checkpoint_3ds_extdata_only_title() {
        let adapter = CheckpointAdapter::new(SystemId::ThreeDS);
        let raw = RawTree {
            files: vec![RawFile {
                path: "0x0055D Pokemon X/extdata/boss/001".into(),
                content: vec![0xCC; 8],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        assert_eq!(saves[0].id.slot, "extdata");
        assert_eq!(saves[0].group_key, "3ds/pokemonx/extdata");
    }

    #[test]
    fn jksv_checkpoint_same_game_converge() {
        let jksv_raw = RawTree {
            files: vec![RawFile {
                path: "Super Mario Odyssey/slot1/save.dat".into(),
                content: vec![1, 2, 3],
            }],
        };
        let checkpoint_raw = RawTree {
            files: vec![RawFile {
                path: "0x0100000000010000 Super Mario Odyssey/slot1/save.dat".into(),
                content: vec![1, 2, 3],
            }],
        };

        let jksv_saves = JksvAdapter::new(SystemId::Switch).normalize(&jksv_raw);
        let checkpoint_saves = CheckpointAdapter::new(SystemId::Switch).normalize(&checkpoint_raw);

        assert_eq!(jksv_saves.len(), 1);
        assert_eq!(checkpoint_saves.len(), 1);
        assert_eq!(jksv_saves[0].id.game.key, checkpoint_saves[0].id.game.key);
        assert_eq!(jksv_saves[0].group_key, checkpoint_saves[0].group_key);
    }
}
