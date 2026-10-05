use alloc::string::String;
use alloc::vec::Vec;
use serde::{Deserialize, Serialize};
use waystone_core::model::{NormalizedSave, SaveId};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NormalizedSaveMeta {
    pub id: SaveId,
    pub group_key: String,
    pub portable: bool,
    pub mtime: String,
}

impl NormalizedSaveMeta {
    pub fn from_core(save: &NormalizedSave) -> Self {
        Self {
            id: save.id.clone(),
            group_key: save.group_key.clone(),
            portable: save.portable,
            mtime: save.mtime.clone(),
        }
    }

    pub fn into_save(self, files: Vec<(String, Vec<u8>)>) -> NormalizedSave {
        NormalizedSave {
            id: self.id,
            group_key: self.group_key,
            portable: self.portable,
            mtime: self.mtime,
            files,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::model::*;

    #[test]
    fn meta_emits_empty_mtime_field() {
        // The shells' json_set_mtime relies on the literal `"mtime":""` substring.
        let save = NormalizedSave {
            id: SaveId {
                source: "jksv".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: "T".into(),
                    display_name: "T".into(),
                    confidence: Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: "switch/T/main".into(),
            portable: true,
            mtime: "".into(),
            files: vec![("data.sav".into(), vec![0xFF; 4])],
        };
        let meta = NormalizedSaveMeta::from_core(&save);
        let json = serde_json::to_string(&meta).unwrap();
        assert!(json.contains(r#""mtime":"""#));
        assert!(json.contains(r#""group_key":"switch/T/main""#));
        assert!(!json.contains("files"));
    }

    #[test]
    fn meta_round_trips_into_save() {
        let json = r#"{"id":{"source":"jksv","system":"switch","game":{"key":"T","display_name":"T","confidence":"strong","title_id":null,"serial":null,"rom_crc":null},"slot":"main","kind":"native"},"group_key":"switch/T/main","portable":true,"mtime":"2026-01-01T00:00:00Z"}"#;
        let meta: NormalizedSaveMeta = serde_json::from_str(json).unwrap();
        let save = meta.into_save(vec![("data.sav".into(), vec![1, 2, 3])]);
        assert_eq!(save.group_key, "switch/T/main");
        assert_eq!(save.files[0].1, vec![1, 2, 3]);
    }
}
