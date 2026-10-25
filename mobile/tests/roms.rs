use waystone_mobile::exports::{
    crc32_update, rom_display_name, rom_identity, rom_keyed_normalize, rom_keyed_slots,
    rom_keyed_to_native, rom_needs_full_hash, rom_pair,
};
use waystone_mobile::types::{RawFileEntry, RawTree};

fn nds_header() -> Vec<u8> {
    let mut h = vec![0u8; 0x200];
    h[0x0C..0x10].copy_from_slice(b"AMCE");
    h[0x15E..0x160].copy_from_slice(&0x1A2Bu16.to_le_bytes());
    h
}

#[test]
fn identity_and_crc_exports() {
    assert!(!rom_needs_full_hash("nds".into(), nds_header()).unwrap());
    assert_eq!(
        rom_identity("nds".into(), nds_header(), None)
            .unwrap()
            .unwrap(),
        "AMCE-1A2B"
    );
    assert!(rom_needs_full_hash("gb".into(), vec![]).unwrap());
    let crc = crc32_update(crc32_update(0, b"1234".to_vec()), b"56789".to_vec());
    assert_eq!(
        rom_identity("gb".into(), vec![], Some(crc))
            .unwrap()
            .unwrap(),
        "CBF43926"
    );
    assert_eq!(rom_identity("gb".into(), vec![], None).unwrap(), None);
    assert_eq!(
        rom_display_name("gb".into(), vec![], "Tetris.gb".into()).unwrap(),
        "Tetris"
    );
    assert!(rom_needs_full_hash("switch".into(), vec![]).is_err());
}

#[test]
fn pairing_export() {
    let pairs = rom_pair(
        vec!["roms/gb/Tetris.gb".into(), "roms/gb/Tetris.sav".into()],
        None,
    )
    .unwrap();
    assert_eq!(pairs.len(), 1);
    assert_eq!(pairs[0].system, "gb");
    assert_eq!(pairs[0].save_dir, "roms/gb");
    assert_eq!(pairs[0].save_paths, vec!["roms/gb/Tetris.sav".to_string()]);
}

#[test]
fn rom_keyed_round_trip_renames_to_local_rom() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "Mario.sav".into(),
            content: vec![7; 8],
        }],
    };
    let saves = rom_keyed_normalize(
        "nds".into(),
        "AMCE-1A2B".into(),
        "MKDS".into(),
        "Mario.nds".into(),
        "native".into(),
        raw,
    )
    .unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].group_key, "nds/AMCE-1A2B/battery");
    let native =
        rom_keyed_to_native(saves[0].clone(), "mkds.nds".into(), "native".into(), vec![]).unwrap();
    assert_eq!(native.files[0].path, "mkds.sav");
    assert_eq!(native.files[0].content, vec![7; 8]);
}

#[test]
fn keyed_slots_export() {
    assert_eq!(
        rom_keyed_slots("gba".into()).unwrap(),
        vec!["battery".to_string()]
    );
    let nds = rom_keyed_slots("nds".into()).unwrap();
    assert_eq!(nds.len(), 20);
    assert_eq!(nds[0], "battery");
    assert!(rom_keyed_slots("switch".into()).is_err());
}

#[test]
fn flavored_pairing_normalize_and_to_native() {
    let pairs = rom_pair(
        vec!["roms/Mario.nds".into(), "@saves/Mario.dsv".into()],
        Some("drastic".into()),
    )
    .unwrap();
    assert_eq!(pairs[0].flavor, "drastic");
    assert_eq!(pairs[0].save_paths, vec!["@saves/Mario.dsv".to_string()]);
    assert!(rom_pair(vec![], Some("azahar".into())).is_err());

    let raw_bytes = vec![0x5Au8; 8192];
    let dsv = waystone_core::dsv::dsv_encode(&raw_bytes, None);
    let saves = rom_keyed_normalize(
        "nds".into(),
        "AMCE-1A2B".into(),
        "Mario".into(),
        "Mario.nds".into(),
        "drastic".into(),
        RawTree {
            files: vec![RawFileEntry {
                path: "Mario.dsv".into(),
                content: dsv.clone(),
            }],
        },
    )
    .unwrap();
    assert_eq!(saves[0].files[0].content, raw_bytes);
    let native = rom_keyed_to_native(
        saves[0].clone(),
        "Mario.nds".into(),
        "drastic".into(),
        vec![RawFileEntry {
            path: "Mario.dsv".into(),
            content: dsv,
        }],
    )
    .unwrap();
    assert_eq!(native.files[0].path, "Mario.dsv");
}
