use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::adapters::Adapter;
use crate::adapters::folder_layout::{folder_layout_to_native, normalize_folder_layout};
use crate::model::*;

pub struct JksvAdapter {
    system: SystemId,
}

impl JksvAdapter {
    pub fn new(system: SystemId) -> Self {
        Self { system }
    }

    fn parse_title_dir(dir_name: &str) -> (String, Option<String>) {
        if let Some(idx) = dir_name.rfind(" - ") {
            let name = dir_name[..idx].to_string();
            let tid = dir_name[idx + 3..].to_string();
            (name, Some(tid))
        } else {
            (dir_name.to_string(), None)
        }
    }
}

impl Adapter for JksvAdapter {
    fn id(&self) -> &str {
        "jksv"
    }

    fn systems(&self) -> &[SystemId] {
        core::slice::from_ref(&self.system)
    }

    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave> {
        normalize_folder_layout(raw, self.system, "jksv", Self::parse_title_dir)
    }

    fn to_native(&self, save: &NormalizedSave) -> RawTree {
        folder_layout_to_native(save, |name, tid| match tid {
            Some(tid) => format!("{} - {}", name, tid),
            None => name.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{Confidence, RawFile, RawTree, SaveKind, SystemId};

    fn make_jksv_tree() -> RawTree {
        RawTree {
            files: vec![
                RawFile {
                    path: "Super Mario Odyssey - 0100000000010000/main/save.dat".into(),
                    content: vec![0xDE, 0xAD],
                },
                RawFile {
                    path: "Super Mario Odyssey - 0100000000010000/main/extra.bin".into(),
                    content: vec![0xBE, 0xEF],
                },
                RawFile {
                    path: "Zelda BOTW - 01007EF00011E000/slot1/game_data.sav".into(),
                    content: vec![1, 2, 3],
                },
            ],
        }
    }

    #[test]
    fn jksv_normalizes_switch_saves() {
        let adapter = JksvAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_jksv_tree());
        assert_eq!(saves.len(), 2);
    }

    #[test]
    fn jksv_extracts_title_id() {
        let adapter = JksvAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_jksv_tree());
        let smo = saves
            .iter()
            .find(|s| s.id.game.key == "0100000000010000")
            .unwrap();
        assert_eq!(smo.id.game.display_name, "Super Mario Odyssey");
        assert_eq!(smo.id.game.title_id, Some("0100000000010000".into()));
        assert_eq!(smo.id.game.confidence, Confidence::Strong);
    }

    #[test]
    fn jksv_groups_by_title_and_slot() {
        let adapter = JksvAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_jksv_tree());
        let smo = saves
            .iter()
            .find(|s| s.id.game.key == "0100000000010000")
            .unwrap();
        assert_eq!(smo.id.slot, "main");
        assert_eq!(smo.files.len(), 2);
        assert_eq!(smo.group_key, "switch/0100000000010000/main");
    }

    #[test]
    fn jksv_sets_kind_native() {
        let adapter = JksvAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_jksv_tree());
        for save in &saves {
            assert_eq!(save.id.kind, SaveKind::Native);
        }
    }

    #[test]
    fn jksv_to_native_round_trips() {
        let adapter = JksvAdapter::new(SystemId::Switch);
        let saves = adapter.normalize(&make_jksv_tree());
        let smo = saves
            .iter()
            .find(|s| s.id.game.key == "0100000000010000")
            .unwrap();
        let native = adapter.to_native(smo);
        assert_eq!(native.files.len(), 2);
        let save_dat = native
            .files
            .iter()
            .find(|f| f.path.ends_with("save.dat"))
            .unwrap();
        assert_eq!(save_dat.content, vec![0xDE, 0xAD]);
    }
}
