use alloc::string::String;
use alloc::vec::Vec;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SystemId {
    Switch,
    #[serde(rename = "3ds")]
    ThreeDS,
    Nds,
    Gba,
    Gbc,
    Gb,
    Nes,
    Snes,
    Md,
    Sms,
    Gg,
    Ngp,
}

impl SystemId {
    pub const ALL: [SystemId; 12] = [
        Self::Switch,
        Self::ThreeDS,
        Self::Nds,
        Self::Gba,
        Self::Gbc,
        Self::Gb,
        Self::Nes,
        Self::Snes,
        Self::Md,
        Self::Sms,
        Self::Gg,
        Self::Ngp,
    ];

    pub fn parse(s: &str) -> Option<SystemId> {
        Self::ALL.into_iter().find(|sys| sys.as_str() == s)
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Switch => "switch",
            Self::ThreeDS => "3ds",
            Self::Nds => "nds",
            Self::Gba => "gba",
            Self::Gbc => "gbc",
            Self::Gb => "gb",
            Self::Nes => "nes",
            Self::Snes => "snes",
            Self::Md => "md",
            Self::Sms => "sms",
            Self::Gg => "gg",
            Self::Ngp => "ngp",
        }
    }
}

impl core::fmt::Display for SystemId {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.write_str(self.as_str())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SaveKind {
    Battery,
    SaveState,
    Native,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Confidence {
    Strong,
    Weak,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GameRef {
    pub key: String,
    pub display_name: String,
    pub confidence: Confidence,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub serial: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rom_crc: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SaveId {
    pub source: String,
    pub system: SystemId,
    pub game: GameRef,
    pub slot: String,
    pub kind: SaveKind,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileRef {
    pub path: String,
    pub size: u64,
    pub hash: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ContentRef {
    pub hash: String,
    pub size: u64,
    pub files: Vec<FileRef>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SaveEntry {
    pub id: SaveId,
    pub group_key: String,
    pub portable: bool,
    pub content: ContentRef,
    pub mtime: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawFile {
    pub path: String,
    pub content: Vec<u8>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawTree {
    pub files: Vec<RawFile>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct NormalizedSave {
    pub id: SaveId,
    pub group_key: String,
    pub portable: bool,
    pub mtime: String,
    pub files: Vec<(String, Vec<u8>)>,
}

pub fn build_group_key(system: SystemId, game_key: &str, slot: &str) -> String {
    format!("{}/{}/{}", system, game_key, slot)
}

pub fn normalize_game_name(name: &str) -> String {
    name.chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .map(|c| c.to_ascii_lowercase())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn system_id_serializes_to_lowercase() {
        let s = serde_json::to_string(&SystemId::Switch).unwrap();
        assert_eq!(s, "\"switch\"");
    }

    #[test]
    fn save_kind_serializes() {
        let s = serde_json::to_string(&SaveKind::Battery).unwrap();
        assert_eq!(s, "\"battery\"");
    }

    #[test]
    fn game_ref_round_trips() {
        let g = GameRef {
            key: "AGB-BPEE".into(),
            display_name: "Pokemon Emerald".into(),
            confidence: Confidence::Strong,
            title_id: None,
            serial: Some("AGB-BPEE".into()),
            rom_crc: None,
        };
        let json = serde_json::to_string(&g).unwrap();
        let g2: GameRef = serde_json::from_str(&json).unwrap();
        assert_eq!(g, g2);
    }

    #[test]
    fn save_entry_round_trips() {
        let entry = SaveEntry {
            id: SaveId {
                source: "jksv".into(),
                system: SystemId::Switch,
                game: GameRef {
                    key: "0100000000010000".into(),
                    display_name: "Super Mario Odyssey".into(),
                    confidence: Confidence::Strong,
                    title_id: Some("0100000000010000".into()),
                    serial: None,
                    rom_crc: None,
                },
                slot: "main".into(),
                kind: SaveKind::Native,
            },
            group_key: "switch/0100000000010000/main".into(),
            portable: true,
            content: ContentRef {
                hash: "abc123".into(),
                size: 1024,
                files: vec![FileRef {
                    path: "save.dat".into(),
                    size: 1024,
                    hash: "def456".into(),
                }],
            },
            mtime: "2026-08-31T12:00:00Z".into(),
        };
        let json = serde_json::to_string_pretty(&entry).unwrap();
        let entry2: SaveEntry = serde_json::from_str(&json).unwrap();
        assert_eq!(entry.id.game.key, entry2.id.game.key);
        assert_eq!(entry.group_key, entry2.group_key);
        assert_eq!(entry.content.hash, entry2.content.hash);
    }

    #[test]
    fn group_key_format() {
        let key = build_group_key(SystemId::Gba, "AGB-BPEE", "battery");
        assert_eq!(key, "gba/AGB-BPEE/battery");
    }

    #[test]
    fn raw_tree_preserves_files() {
        let tree = RawTree {
            files: vec![
                RawFile {
                    path: "save.dat".into(),
                    content: vec![1, 2, 3],
                },
                RawFile {
                    path: "extra.bin".into(),
                    content: vec![4, 5],
                },
            ],
        };
        assert_eq!(tree.files.len(), 2);
        assert_eq!(tree.files[0].content, vec![1, 2, 3]);
    }

    #[test]
    fn normalize_game_name_strips_non_alphanumeric() {
        assert_eq!(
            normalize_game_name("Super Smash Bros. Ultimate\u{2122}"),
            "supersmashbrosultimate"
        );
    }

    #[test]
    fn new_rom_systems_round_trip_through_parse_and_serde() {
        for s in SystemId::ALL {
            assert_eq!(SystemId::parse(s.as_str()), Some(s));
            let json = serde_json::to_string(&s).unwrap();
            assert_eq!(json, format!("\"{}\"", s.as_str()));
        }
        assert_eq!(SystemId::parse("md"), Some(SystemId::Md));
        assert_eq!(SystemId::parse("megadrive"), None);
    }
}
