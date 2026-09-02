use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use crate::model::*;

pub(crate) fn normalize_folder_layout(
    raw: &RawTree,
    system: SystemId,
    source: &str,
    parse_title_dir: impl Fn(&str) -> (String, Option<String>),
) -> Vec<NormalizedSave> {
    type SaveFiles = Vec<(String, Vec<u8>)>;
    let mut groups: BTreeMap<(String, String), SaveFiles> = BTreeMap::new();

    for file in &raw.files {
        let parts: Vec<&str> = file.path.splitn(3, '/').collect();
        if parts.len() < 3 {
            continue;
        }
        let title_dir = parts[0].to_string();
        let slot_dir = parts[1].to_string();
        let rel_path = parts[2].to_string();
        groups
            .entry((title_dir, slot_dir))
            .or_default()
            .push((rel_path, file.content.clone()));
    }

    groups
        .into_iter()
        .map(|((title_dir, slot), files)| {
            let (display_name, title_id) = parse_title_dir(&title_dir);
            let key = normalize_game_name(&display_name);
            let confidence = if title_id.is_some() {
                Confidence::Strong
            } else {
                Confidence::Weak
            };

            NormalizedSave {
                id: SaveId {
                    source: source.into(),
                    system,
                    game: GameRef {
                        key: key.clone(),
                        display_name,
                        confidence,
                        title_id,
                        serial: None,
                        rom_crc: None,
                    },
                    slot: slot.clone(),
                    kind: SaveKind::Native,
                },
                group_key: build_group_key(system, &key, &slot),
                portable: true,
                mtime: String::new(),
                files,
            }
        })
        .collect()
}

pub(crate) fn folder_layout_to_native(
    save: &NormalizedSave,
    format_title_dir: impl Fn(&str, Option<&str>) -> String,
) -> RawTree {
    let title_dir = format_title_dir(&save.id.game.display_name, save.id.game.title_id.as_deref());
    RawTree {
        files: save
            .files
            .iter()
            .map(|(rel, content)| RawFile {
                path: format!("{}/{}/{}", title_dir, save.id.slot, rel),
                content: content.clone(),
            })
            .collect(),
    }
}
