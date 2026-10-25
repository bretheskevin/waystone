use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;

use crate::adapters::Adapter;
use crate::dsv::{dsv_decode, dsv_encode, dsv_split};
use crate::model::*;
use crate::rom_flavor::{SaveCodec, SaveFlavor, classify_flavored, flavored_native_name};
use crate::rom_systems::{base_name, parse_canonical_file_name};

pub struct RomKeyedAdapter {
    system: SystemId,
    rom_id: String,
    display_name: String,
    rom_file_name: String,
    flavor: SaveFlavor,
}

impl RomKeyedAdapter {
    pub fn new(
        system: SystemId,
        rom_id: impl Into<String>,
        display_name: impl Into<String>,
        rom_file_name: impl Into<String>,
    ) -> Self {
        Self {
            system,
            rom_id: rom_id.into(),
            display_name: display_name.into(),
            rom_file_name: rom_file_name.into(),
            flavor: SaveFlavor::Native,
        }
    }

    pub fn with_flavor(mut self, flavor: SaveFlavor) -> Self {
        self.flavor = flavor;
        self
    }

    fn make_save(&self, slot: String, files: Vec<(String, Vec<u8>)>) -> NormalizedSave {
        NormalizedSave {
            id: SaveId {
                source: "rom_keyed".into(),
                system: self.system,
                game: GameRef {
                    key: self.rom_id.clone(),
                    display_name: self.display_name.clone(),
                    confidence: Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: slot.clone(),
                kind: SaveKind::Battery,
            },
            group_key: build_group_key(self.system, &self.rom_id, &slot),
            portable: true,
            mtime: String::new(),
            files,
        }
    }
}

pub fn rom_keyed_to_native(
    system: SystemId,
    slot: &str,
    rom_file_name: &str,
    files: &[(String, Vec<u8>)],
) -> Vec<(String, Vec<u8>)> {
    rom_keyed_to_native_flavored(system, slot, rom_file_name, SaveFlavor::Native, files, &[])
}

/// [rom_keyed_to_native] for a save flavor; `existing` = the ROM's current local save files
/// (base names), used to keep an existing file name/variant and its `.dsv` footer (a footerless
/// local `.dsv` stays footerless).
pub fn rom_keyed_to_native_flavored(
    system: SystemId,
    slot: &str,
    rom_file_name: &str,
    flavor: SaveFlavor,
    files: &[(String, Vec<u8>)],
    existing: &[(String, Vec<u8>)],
) -> Vec<(String, Vec<u8>)> {
    files
        .iter()
        .filter_map(|(name, content)| {
            let kind = parse_canonical_file_name(base_name(name))?;
            if kind.slot() != slot {
                return None;
            }
            let local = existing.iter().find(|(n, _)| {
                classify_flavored(flavor, system, rom_file_name, n).map(|(k, _)| k) == Some(kind)
            });
            let (native, codec) = flavored_native_name(
                flavor,
                system,
                rom_file_name,
                &kind,
                local.map(|(n, _)| n.as_str()),
            )?;
            let bytes = match (codec, local) {
                (SaveCodec::Raw, _) => content.clone(),
                (SaveCodec::Dsv, None) => dsv_encode(content, None),
                (SaveCodec::Dsv, Some((_, b))) => match dsv_split(b).1 {
                    Some(footer) => dsv_encode(content, Some(footer)),
                    None => content.clone(),
                },
            };
            Some((native, bytes))
        })
        .collect()
}

impl Adapter for RomKeyedAdapter {
    fn id(&self) -> &str {
        "rom_keyed"
    }

    fn systems(&self) -> &[SystemId] {
        core::slice::from_ref(&self.system)
    }

    fn normalize(&self, raw: &RawTree) -> Vec<NormalizedSave> {
        let mut by_slot: BTreeMap<String, Vec<(String, Vec<u8>)>> = BTreeMap::new();
        for file in &raw.files {
            let Some((kind, codec)) = classify_flavored(
                self.flavor,
                self.system,
                &self.rom_file_name,
                base_name(&file.path),
            ) else {
                continue;
            };
            let content = match codec {
                SaveCodec::Raw => file.content.clone(),
                SaveCodec::Dsv => dsv_decode(&file.content),
            };
            by_slot
                .entry(kind.slot())
                .or_default()
                .push((kind.canonical_file_name(), content));
        }
        by_slot
            .into_iter()
            .map(|(slot, mut files)| {
                files.sort_by(|a, b| a.0.cmp(&b.0));
                files.dedup_by(|a, b| a.0 == b.0);
                self.make_save(slot, files)
            })
            .collect()
    }

    fn to_native(&self, save: &NormalizedSave) -> RawTree {
        RawTree {
            files: rom_keyed_to_native_flavored(
                save.id.system,
                &save.id.slot,
                &self.rom_file_name,
                self.flavor,
                &save.files,
                &[],
            )
            .into_iter()
            .map(|(path, content)| RawFile { path, content })
            .collect(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::packaging::package;
    use alloc::string::ToString;

    fn raw(names: &[(&str, u8)]) -> RawTree {
        RawTree {
            files: names
                .iter()
                .map(|(n, b)| RawFile {
                    path: (*n).into(),
                    content: vec![*b; 16],
                })
                .collect(),
        }
    }

    fn nds(rom_file_name: &str) -> RomKeyedAdapter {
        RomKeyedAdapter::new(SystemId::Nds, "AMCE-1A2B", "Mario Kart DS", rom_file_name)
    }

    #[test]
    fn battery_slot_and_dsiware_saves() {
        let saves = nds("Mario.nds").normalize(&raw(&[
            ("roms/nds/saves/Mario.sav", 1),
            ("Mario.sav1", 2),
            ("Mario.pub", 3),
            ("Mario.prv", 4),
            ("Mario.pu2", 5),
            ("Mario.pr2", 6),
            ("usrcheat.dat", 7),
            ("Other.sav", 8),
        ]));
        assert_eq!(saves.len(), 4);
        let by = |slot: &str| saves.iter().find(|s| s.id.slot == slot).unwrap();
        assert_eq!(by("battery").files, vec![("battery".into(), vec![1u8; 16])]);
        assert_eq!(by("slot-1").files[0].0, "slot-1");
        assert_eq!(
            by("dsiware")
                .files
                .iter()
                .map(|f| f.0.as_str())
                .collect::<Vec<_>>(),
            vec!["dsiware.prv", "dsiware.pub"]
        );
        assert_eq!(by("dsiware-2").files.len(), 2);
    }

    #[test]
    fn identity_metadata_and_group_key() {
        let saves = nds("Mario.nds").normalize(&raw(&[("Mario.sav", 1)]));
        let s = &saves[0];
        assert_eq!(s.group_key, "nds/AMCE-1A2B/battery");
        assert_eq!(s.id.game.key, "AMCE-1A2B");
        assert_eq!(s.id.game.display_name, "Mario Kart DS");
        assert_eq!(s.id.game.confidence, Confidence::Strong);
        assert_eq!(s.id.source, "rom_keyed");
        assert_eq!(s.id.kind, SaveKind::Battery);
        assert!(s.portable);
    }

    #[test]
    fn different_stems_converge_on_same_content_hash() {
        let a = nds("Mario Kart DS (U).nds").normalize(&raw(&[("Mario Kart DS (U).sav", 9)]));
        let b = nds("mkds.nds").normalize(&raw(&[("mkds.sav", 9)]));
        assert_eq!(a[0].group_key, b[0].group_key);
        assert_eq!(package(&a[0]).0.content.hash, package(&b[0]).0.content.hash);
    }

    #[test]
    fn to_native_uses_the_local_rom_name() {
        let remote = nds("Mario Kart DS (U).nds").normalize(&raw(&[("Mario Kart DS (U).sav2", 9)]));
        let native = nds("mkds.nds").to_native(&remote[0]);
        assert_eq!(native.files.len(), 1);
        assert_eq!(native.files[0].path, "mkds.sav2");
        assert_eq!(native.files[0].content, vec![9u8; 16]);
    }

    #[test]
    fn other_systems_name_their_saves() {
        let snes = RomKeyedAdapter::new(SystemId::Snes, "0BADF00D", "Mario", "Mario.sfc");
        let s = snes.normalize(&raw(&[("Mario.srm", 1), ("Mario.sav", 2)]));
        assert_eq!(s.len(), 1);
        assert_eq!(snes.to_native(&s[0]).files[0].path, "Mario.srm");
        let ngp = RomKeyedAdapter::new(SystemId::Ngp, "12345678", "Sonic", "Sonic.ngp");
        let n = ngp.normalize(&raw(&[("data/ngpds/Sonic.ngp.fla", 1)]));
        assert_eq!(ngp.to_native(&n[0]).files[0].path, "Sonic.ngp.fla");
    }

    #[test]
    fn rom_keyed_to_native_filters_by_slot() {
        let files = vec![
            ("battery".to_string(), vec![1u8]),
            ("slot-3".to_string(), vec![2u8]),
            ("junk".to_string(), vec![3u8]),
        ];
        let out = rom_keyed_to_native(SystemId::Nds, "slot-3", "A.nds", &files);
        assert_eq!(out, vec![("A.sav3".to_string(), vec![2u8])]);
    }

    #[test]
    fn drastic_dsv_normalizes_to_the_same_hash_as_a_raw_sav() {
        let raw_bytes = vec![0x42u8; 8192];
        let dsv = crate::dsv::dsv_encode(&raw_bytes, None);
        let drastic = nds("Mario.nds")
            .with_flavor(SaveFlavor::Drastic)
            .normalize(&RawTree {
                files: vec![RawFile {
                    path: "Mario.dsv".into(),
                    content: dsv,
                }],
            });
        let native = nds("mkds.nds").normalize(&RawTree {
            files: vec![RawFile {
                path: "mkds.sav".into(),
                content: raw_bytes.clone(),
            }],
        });
        assert_eq!(drastic[0].files, vec![("battery".to_string(), raw_bytes)]);
        assert_eq!(
            package(&drastic[0]).0.content.hash,
            package(&native[0]).0.content.hash
        );
    }

    #[test]
    fn flavored_to_native_keeps_the_local_footer_and_name() {
        let existing = crate::dsv::dsv_encode(&[0u8; 8192], None);
        let files = vec![("battery".to_string(), vec![1u8; 8192])];
        let out = rom_keyed_to_native_flavored(
            SystemId::Nds,
            "battery",
            "Mario.nds",
            SaveFlavor::RetroArch,
            &files,
            &[("Mario.dsv".to_string(), existing.clone())],
        );
        assert_eq!(out[0].0, "Mario.dsv");
        assert_eq!(
            crate::dsv::dsv_split(&out[0].1).1,
            crate::dsv::dsv_split(&existing).1
        );
        assert_eq!(crate::dsv::dsv_decode(&out[0].1), vec![1u8; 8192]);
    }

    #[test]
    fn footerless_local_dsv_is_rewritten_footerless() {
        let files = vec![("battery".to_string(), vec![1u8; 8192])];
        let out = rom_keyed_to_native_flavored(
            SystemId::Nds,
            "battery",
            "Mario.nds",
            SaveFlavor::Drastic,
            &files,
            &[("Mario.dsv".to_string(), vec![0u8; 8192])],
        );
        assert_eq!(out, vec![("Mario.dsv".to_string(), vec![1u8; 8192])]);
    }

    #[test]
    fn retroarch_srm_round_trip_and_native_default_unchanged() {
        let gba = RomKeyedAdapter::new(SystemId::Gba, "AXVE-01", "Ruby", "Ruby.gba")
            .with_flavor(SaveFlavor::RetroArch);
        let s = gba.normalize(&raw(&[("Ruby.srm", 6), ("Ruby.sav", 7)]));
        assert_eq!(s.len(), 1);
        assert_eq!(s[0].files[0].1, vec![6u8; 16]);
        assert_eq!(gba.to_native(&s[0]).files[0].path, "Ruby.srm");
        assert_eq!(
            rom_keyed_to_native(
                SystemId::Nds,
                "battery",
                "A.nds",
                &[("battery".into(), vec![1])]
            ),
            vec![("A.sav".to_string(), vec![1u8])]
        );
    }
}
