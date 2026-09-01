use base64::Engine;
use base64::engine::general_purpose::STANDARD as B64;
use serde::{Deserialize, Serialize};
use waystone_core::model::{NormalizedSave, RawFile, RawTree, SaveId};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileEntryDto {
    pub path: String,
    pub data_b64: String,
}

impl FileEntryDto {
    pub fn from_parts(path: &str, data: &[u8]) -> Self {
        Self {
            path: path.to_string(),
            data_b64: B64.encode(data),
        }
    }

    pub fn decode_data(&self) -> Result<Vec<u8>, base64::DecodeError> {
        B64.decode(&self.data_b64)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RawTreeDto {
    pub files: Vec<FileEntryDto>,
}

impl RawTreeDto {
    pub fn from_core(tree: &RawTree) -> Self {
        Self {
            files: tree
                .files
                .iter()
                .map(|f| FileEntryDto::from_parts(&f.path, &f.content))
                .collect(),
        }
    }

    pub fn to_core(&self) -> Result<RawTree, base64::DecodeError> {
        let files = self
            .files
            .iter()
            .map(|f| {
                let content = f.decode_data()?;
                Ok(RawFile {
                    path: f.path.clone(),
                    content,
                })
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(RawTree { files })
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NormalizedSaveDto {
    pub id: SaveId,
    pub group_key: String,
    pub portable: bool,
    pub mtime: String,
    pub files: Vec<FileEntryDto>,
}

impl NormalizedSaveDto {
    pub fn from_core(save: &NormalizedSave) -> Self {
        Self {
            id: save.id.clone(),
            group_key: save.group_key.clone(),
            portable: save.portable,
            mtime: save.mtime.clone(),
            files: save
                .files
                .iter()
                .map(|(path, data)| FileEntryDto::from_parts(path, data))
                .collect(),
        }
    }

    pub fn to_core(&self) -> Result<NormalizedSave, base64::DecodeError> {
        let files = self
            .files
            .iter()
            .map(|f| {
                let data = f.decode_data()?;
                Ok((f.path.clone(), data))
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(NormalizedSave {
            id: self.id.clone(),
            group_key: self.group_key.clone(),
            portable: self.portable,
            mtime: self.mtime.clone(),
            files,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use waystone_core::model::*;

    #[test]
    fn raw_tree_dto_round_trips() {
        let tree = RawTree {
            files: vec![RawFile {
                path: "save.dat".into(),
                content: vec![0xCA, 0xFE, 0xBA, 0xBE],
            }],
        };
        let dto = RawTreeDto::from_core(&tree);
        let json = serde_json::to_string(&dto).unwrap();
        assert!(json.contains("data_b64"));
        assert!(!json.contains("[202,")); // no int-array
        let parsed: RawTreeDto = serde_json::from_str(&json).unwrap();
        let restored = parsed.to_core().unwrap();
        assert_eq!(restored.files[0].path, "save.dat");
        assert_eq!(restored.files[0].content, vec![0xCA, 0xFE, 0xBA, 0xBE]);
    }

    #[test]
    fn normalized_save_dto_round_trips() {
        let save = NormalizedSave {
            id: SaveId {
                source: "jksv".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: "TESTGAME".into(),
                    display_name: "Test Game".into(),
                    confidence: Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: "switch/TESTGAME/main".into(),
            portable: true,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![("data.sav".into(), vec![0xFF; 16])],
        };
        let dto = NormalizedSaveDto::from_core(&save);
        let json = serde_json::to_string(&dto).unwrap();
        let parsed: NormalizedSaveDto = serde_json::from_str(&json).unwrap();
        let restored = parsed.to_core().unwrap();
        assert_eq!(restored.files[0].0, "data.sav");
        assert_eq!(restored.files[0].1, vec![0xFF; 16]);
        assert_eq!(restored.id.game.key, "TESTGAME");
    }

    #[test]
    fn file_entry_dto_round_trips() {
        let dto = FileEntryDto {
            path: "test.bin".into(),
            data_b64: B64.encode(vec![1, 2, 3]),
        };
        let json = serde_json::to_string(&dto).unwrap();
        let parsed: FileEntryDto = serde_json::from_str(&json).unwrap();
        let bytes = parsed.decode_data().unwrap();
        assert_eq!(bytes, vec![1, 2, 3]);
    }
}
