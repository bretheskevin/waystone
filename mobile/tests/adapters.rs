use waystone_mobile::exports::{
    checkpoint_normalize, checkpoint_to_native, jksv_normalize, jksv_to_native, mgba_normalize,
    mgba_to_native,
};
use waystone_mobile::types::{RawFileEntry, RawTree};

#[test]
fn jksv_normalize_and_to_native_round_trip() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "TestGame - 0100AAAA00001000/slot0/data.sav".into(),
            content: vec![0xCA, 0xFE, 0xBA, 0xBE],
        }],
    };
    let saves = jksv_normalize("switch".into(), raw).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].system, "switch");
    assert_eq!(saves[0].slot, "slot0");

    let native = jksv_to_native(saves[0].clone()).unwrap();
    assert_eq!(native.files.len(), 1);
    assert_eq!(native.files[0].content, vec![0xCA, 0xFE, 0xBA, 0xBE]);
}

#[test]
fn jksv_normalize_invalid_system_errors() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "TestGame/slot0/data.sav".into(),
            content: vec![0x01],
        }],
    };
    let result = jksv_normalize("megadrive".into(), raw);
    assert!(result.is_err());
}

#[test]
fn mgba_normalize_and_to_native_round_trip() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "Emerald.sav".into(),
            content: vec![0xFF; 64],
        }],
    };
    let saves = mgba_normalize("gba".into(), raw).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].game_key, "Emerald");
    assert_eq!(saves[0].system, "gba");
    assert_eq!(saves[0].slot, "battery");

    let native = mgba_to_native(saves[0].clone()).unwrap();
    assert_eq!(native.files.len(), 1);
    assert_eq!(native.files[0].path, "Emerald.sav");
    assert_eq!(native.files[0].content, vec![0xFF; 64]);
}

#[test]
fn mgba_normalize_invalid_system_errors() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "Emerald.sav".into(),
            content: vec![0xFF; 8],
        }],
    };
    let result = mgba_normalize("megadrive".into(), raw);
    assert!(result.is_err());
}

#[test]
fn checkpoint_normalize_and_to_native_round_trip_switch() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "0x0100AAAA00001000 TestGame/slot0/save.dat".into(),
            content: vec![0xDE, 0xAD],
        }],
    };
    let saves = checkpoint_normalize("switch".into(), raw).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].system, "switch");
    assert!(saves[0].title_id.is_some());

    let native = checkpoint_to_native(saves[0].clone()).unwrap();
    assert_eq!(native.files.len(), 1);
    assert_eq!(native.files[0].content, vec![0xDE, 0xAD]);
}

#[test]
fn checkpoint_normalize_and_to_native_round_trip_3ds() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "0x01234 My3DSGame/slot0/save.dat".into(),
            content: vec![0xBE, 0xEF],
        }],
    };
    let saves = checkpoint_normalize("3ds".into(), raw).unwrap();
    assert_eq!(saves.len(), 1);
    assert_eq!(saves[0].system, "3ds");

    let native = checkpoint_to_native(saves[0].clone()).unwrap();
    assert_eq!(native.files.len(), 1);
    assert_eq!(native.files[0].content, vec![0xBE, 0xEF]);
}

#[test]
fn checkpoint_normalize_invalid_system_errors() {
    let raw = RawTree {
        files: vec![RawFileEntry {
            path: "0x0100AAAA00001000 TestGame/slot0/save.dat".into(),
            content: vec![0x01],
        }],
    };
    let result = checkpoint_normalize("megadrive".into(), raw);
    assert!(result.is_err());
}
