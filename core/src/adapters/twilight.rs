use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::adapters::Adapter;
use crate::model::*;

pub struct TwilightAdapter {
    system: SystemId,
}

impl Default for TwilightAdapter {
    fn default() -> Self {
        Self::new()
    }
}

type DsiWarePair = (Option<(String, Vec<u8>)>, Option<(String, Vec<u8>)>);

enum FileKind {
    DsiWarePub,
    DsiWarePrv,
    Slot(String),
    Battery,
}

impl TwilightAdapter {
    pub fn new() -> Self {
        Self {
            system: SystemId::Nds,
        }
    }

    fn parse_filename(path: &str) -> Option<(String, FileKind)> {
        let filename = path.rsplit('/').next().unwrap_or(path);

        if let Some(stem) = filename.strip_suffix(".pub")
            && !stem.is_empty()
        {
            return Some((stem.to_string(), FileKind::DsiWarePub));
        }
        if let Some(stem) = filename.strip_suffix(".prv")
            && !stem.is_empty()
        {
            return Some((stem.to_string(), FileKind::DsiWarePrv));
        }

        if let Some(rest) = filename.strip_suffix(".sav") {
            if let Some(nds_pos) = rest.rfind(".nds.") {
                let after_nds = &rest[nds_pos + 5..];
                if !after_nds.is_empty() && after_nds.chars().all(|c| c.is_ascii_digit()) {
                    let stem = &rest[..nds_pos];
                    if !stem.is_empty() {
                        return Some((stem.to_string(), FileKind::Slot(after_nds.to_string())));
                    }
                }
            }
            if !rest.is_empty() {
                return Some((rest.to_string(), FileKind::Battery));
            }
        }

        None
    }

    fn make_save(
        &self,
        stem: String,
        slot: String,
        files: Vec<(String, Vec<u8>)>,
    ) -> NormalizedSave {
        NormalizedSave {
            id: SaveId {
                source: "twilight".into(),
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
                kind: SaveKind::Battery,
            },
            group_key: build_group_key(self.system, &stem, &slot),
            portable: true,
            mtime: String::new(),
            files,
        }
    }
}

impl Adapter for TwilightAdapter {
    fn id(&self) -> &str {
        "twilight"
    }

    fn systems(&self) -> &[SystemId] {
        core::slice::from_ref(&self.system)
    }

    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave> {
        let mut saves = Vec::new();
        let mut dsiware: BTreeMap<String, DsiWarePair> = BTreeMap::new();

        for file in &raw.files {
            let Some((stem, file_kind)) = Self::parse_filename(&file.path) else {
                continue;
            };
            let filename = file.path.rsplit('/').next().unwrap_or(&file.path);

            match file_kind {
                FileKind::DsiWarePub => {
                    let entry = dsiware.entry(stem).or_insert((None, None));
                    entry.0 = Some((filename.to_string(), file.content.clone()));
                }
                FileKind::DsiWarePrv => {
                    let entry = dsiware.entry(stem).or_insert((None, None));
                    entry.1 = Some((filename.to_string(), file.content.clone()));
                }
                FileKind::Slot(n) => {
                    let slot = format!("slot-{}", n);
                    let rel_name = format!("{}.nds.{}.sav", stem, n);
                    saves.push(self.make_save(stem, slot, vec![(rel_name, file.content.clone())]));
                }
                FileKind::Battery => {
                    let rel_name = format!("{}.sav", stem);
                    saves.push(self.make_save(
                        stem,
                        "battery".to_string(),
                        vec![(rel_name, file.content.clone())],
                    ));
                }
            }
        }

        for (stem, (pub_opt, prv_opt)) in dsiware {
            let mut files = Vec::new();
            if let Some(f) = pub_opt {
                files.push(f);
            }
            if let Some(f) = prv_opt {
                files.push(f);
            }
            if !files.is_empty() {
                saves.push(self.make_save(stem, "dsiware".to_string(), files));
            }
        }

        saves
    }

    fn to_native(&self, save: &NormalizedSave) -> RawTree {
        let name = &save.id.game.display_name;
        let slot = &save.id.slot;

        if slot == "dsiware" {
            return RawTree {
                files: save
                    .files
                    .iter()
                    .map(|(rel, content)| RawFile {
                        path: format!("saves/{}", rel),
                        content: content.clone(),
                    })
                    .collect(),
            };
        }

        let native_name = if slot == "battery" {
            format!("{}.sav", name)
        } else if let Some(n) = slot.strip_prefix("slot-") {
            format!("{}.nds.{}.sav", name, n)
        } else {
            format!("{}.sav", name)
        };

        RawTree {
            files: save
                .files
                .iter()
                .map(|(_rel, content)| RawFile {
                    path: format!("saves/{}", native_name),
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

    fn make_twilight_tree() -> RawTree {
        RawTree {
            files: vec![
                RawFile {
                    path: "saves/Pokemon Diamond.sav".into(),
                    content: vec![0xFF; 512],
                },
                RawFile {
                    path: "saves/Pokemon Diamond.nds.1.sav".into(),
                    content: vec![0xAA; 256],
                },
                RawFile {
                    path: "saves/Cooking Coach.pub".into(),
                    content: vec![0x01; 64],
                },
                RawFile {
                    path: "saves/Cooking Coach.prv".into(),
                    content: vec![0x02; 128],
                },
                RawFile {
                    path: "saves/usrcheat.dat".into(),
                    content: vec![0x00; 32],
                },
            ],
        }
    }

    #[test]
    fn twilight_normalizes_battery_save() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let battery = saves
            .iter()
            .find(|s| s.id.game.key == "Pokemon Diamond" && s.id.slot == "battery")
            .expect("battery save not found");
        assert_eq!(battery.id.kind, SaveKind::Battery);
        assert_eq!(battery.id.system, SystemId::Nds);
        assert_eq!(battery.id.source, "twilight");
        assert!(battery.portable);
        assert_eq!(battery.files.len(), 1);
        assert_eq!(battery.files[0].0, "Pokemon Diamond.sav");
        assert_eq!(battery.files[0].1, vec![0xFF; 512]);
    }

    #[test]
    fn twilight_normalizes_numbered_slot() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let slot = saves
            .iter()
            .find(|s| s.id.game.key == "Pokemon Diamond" && s.id.slot == "slot-1")
            .expect("slot save not found");
        assert_eq!(slot.id.kind, SaveKind::Battery);
        assert_eq!(slot.id.system, SystemId::Nds);
        assert!(slot.portable);
        assert_eq!(slot.files.len(), 1);
        assert_eq!(slot.files[0].0, "Pokemon Diamond.nds.1.sav");
        assert_eq!(slot.files[0].1, vec![0xAA; 256]);
    }

    #[test]
    fn twilight_groups_dsiware_pub_prv() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let dsiware = saves
            .iter()
            .find(|s| s.id.game.key == "Cooking Coach" && s.id.slot == "dsiware")
            .expect("dsiware save not found");
        assert_eq!(dsiware.id.kind, SaveKind::Battery);
        assert_eq!(dsiware.id.system, SystemId::Nds);
        assert!(dsiware.portable);
        assert_eq!(dsiware.files.len(), 2);
        let has_pub = dsiware
            .files
            .iter()
            .any(|(name, _)| name == "Cooking Coach.pub");
        let has_prv = dsiware
            .files
            .iter()
            .any(|(name, _)| name == "Cooking Coach.prv");
        assert!(has_pub, "dsiware save must include .pub file");
        assert!(has_prv, "dsiware save must include .prv file");
    }

    #[test]
    fn twilight_all_saves_are_battery_portable_nds() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        for save in &saves {
            assert_eq!(save.id.kind, SaveKind::Battery);
            assert_eq!(save.id.system, SystemId::Nds);
            assert!(save.portable);
        }
    }

    #[test]
    fn twilight_game_key_is_stem_with_weak_confidence() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        for save in &saves {
            assert_eq!(save.id.game.confidence, Confidence::Weak);
            assert_eq!(save.id.game.key, save.id.game.display_name);
            assert!(save.id.game.title_id.is_none());
            assert!(save.id.game.serial.is_none());
            assert!(save.id.game.rom_crc.is_none());
        }
    }

    #[test]
    fn twilight_ignores_non_matching_files() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let usrcheat = saves.iter().find(|s| s.id.game.key == "usrcheat");
        assert!(usrcheat.is_none(), "usrcheat.dat should be ignored");
        assert_eq!(saves.len(), 3);
    }

    #[test]
    fn twilight_group_key_format() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let battery = saves
            .iter()
            .find(|s| s.id.game.key == "Pokemon Diamond" && s.id.slot == "battery")
            .unwrap();
        assert_eq!(battery.group_key, "nds/Pokemon Diamond/battery");
    }

    #[test]
    fn twilight_to_native_round_trips_battery() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let battery = saves
            .iter()
            .find(|s| s.id.game.key == "Pokemon Diamond" && s.id.slot == "battery")
            .unwrap();
        let native = adapter.to_native(battery);
        assert_eq!(native.files.len(), 1);
        assert_eq!(native.files[0].path, "saves/Pokemon Diamond.sav");
        assert_eq!(native.files[0].content, vec![0xFF; 512]);
    }

    #[test]
    fn twilight_to_native_round_trips_slot() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let slot = saves
            .iter()
            .find(|s| s.id.game.key == "Pokemon Diamond" && s.id.slot == "slot-1")
            .unwrap();
        let native = adapter.to_native(slot);
        assert_eq!(native.files.len(), 1);
        assert_eq!(native.files[0].path, "saves/Pokemon Diamond.nds.1.sav");
        assert_eq!(native.files[0].content, vec![0xAA; 256]);
    }

    #[test]
    fn twilight_non_digit_slot_falls_through_to_battery() {
        let adapter = TwilightAdapter::new();
        let raw = RawTree {
            files: vec![RawFile {
                path: "saves/Game.nds.x.sav".into(),
                content: vec![0xCC; 16],
            }],
        };
        let saves = adapter.normalize(&raw);
        assert_eq!(saves.len(), 1);
        let save = &saves[0];
        assert_eq!(save.id.slot, "battery");
        assert_eq!(save.id.game.key, "Game.nds.x");
        assert_eq!(save.files[0].0, "Game.nds.x.sav");
    }

    #[test]
    fn twilight_to_native_round_trips_dsiware() {
        let adapter = TwilightAdapter::new();
        let saves = adapter.normalize(&make_twilight_tree());
        let dsiware = saves
            .iter()
            .find(|s| s.id.game.key == "Cooking Coach" && s.id.slot == "dsiware")
            .unwrap();
        let native = adapter.to_native(dsiware);
        assert_eq!(native.files.len(), 2);
        let pub_file = native
            .files
            .iter()
            .find(|f| f.path == "saves/Cooking Coach.pub");
        let prv_file = native
            .files
            .iter()
            .find(|f| f.path == "saves/Cooking Coach.prv");
        assert!(pub_file.is_some(), "native must include .pub");
        assert!(prv_file.is_some(), "native must include .prv");
        assert_eq!(pub_file.unwrap().content, vec![0x01; 64]);
        assert_eq!(prv_file.unwrap().content, vec![0x02; 128]);
    }
}
