use alloc::string::{String, ToString};

use crate::crc32::crc32;
use crate::model::SystemId;
use crate::rom_systems::{IdScheme, base_name, file_stem, rom_system};

pub const ROM_HEADER_LEN: usize = 0x200;

const NDS_CODE: usize = 0x0C;
const NDS_HEADER_CRC16: usize = 0x15E;
const GBA_CODE: usize = 0xAC;
const GBA_COMPLEMENT: usize = 0xBD;

fn game_code(header: &[u8], off: usize) -> Option<String> {
    let raw = header.get(off..off + 4)?;
    let mut code = String::with_capacity(4);
    for &b in raw {
        let c = b.to_ascii_uppercase();
        if !(c.is_ascii_uppercase() || c.is_ascii_digit()) {
            return None;
        }
        code.push(c as char);
    }
    if code.bytes().all(|c| c == b'0') {
        return None;
    }
    Some(code)
}

fn header_identity(scheme: IdScheme, header: &[u8]) -> Option<String> {
    match scheme {
        IdScheme::NdsHeader => {
            let code = game_code(header, NDS_CODE)?;
            let crc = header.get(NDS_HEADER_CRC16..NDS_HEADER_CRC16 + 2)?;
            Some(format!(
                "{}-{:04X}",
                code,
                u16::from_le_bytes([crc[0], crc[1]])
            ))
        }
        IdScheme::GbaHeader => {
            let code = game_code(header, GBA_CODE)?;
            Some(format!("{}-{:02X}", code, header.get(GBA_COMPLEMENT)?))
        }
        IdScheme::Crc32 => None,
    }
}

pub fn needs_full_hash(system: SystemId, header: &[u8]) -> bool {
    match rom_system(system) {
        Some(rs) => header_identity(rs.id_scheme, header).is_none(),
        None => true,
    }
}

pub fn rom_identity(system: SystemId, header: &[u8], full_crc32: Option<u32>) -> Option<String> {
    let rs = rom_system(system)?;
    if let Some(id) = header_identity(rs.id_scheme, header) {
        return Some(id);
    }
    full_crc32.map(|c| format!("{:08X}", c))
}

pub fn rom_identity_from_bytes(system: SystemId, rom: &[u8]) -> Option<String> {
    let header = &rom[..rom.len().min(ROM_HEADER_LEN)];
    let crc = needs_full_hash(system, header).then(|| crc32(rom));
    rom_identity(system, header, crc)
}

// Always the file stem: ROM hacks keep the base game's GBA header title. NDS banner titles are
// read by the shells; system/header stay so the FFI/UniFFI signatures remain stable.
pub fn display_name(_system: SystemId, _header: &[u8], rom_file_name: &str) -> String {
    file_stem(base_name(rom_file_name)).to_string()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::crc32::crc32;

    fn nds_header(code: &[u8; 4], crc16: u16) -> alloc::vec::Vec<u8> {
        let mut h = vec![0u8; ROM_HEADER_LEN];
        h[0x0C..0x10].copy_from_slice(code);
        h[0x15E..0x160].copy_from_slice(&crc16.to_le_bytes());
        h
    }

    fn gba_header(title: &[u8], code: &[u8; 4], chk: u8) -> alloc::vec::Vec<u8> {
        let mut h = vec![0u8; ROM_HEADER_LEN];
        h[0xA0..0xA0 + title.len()].copy_from_slice(title);
        h[0xAC..0xB0].copy_from_slice(code);
        h[0xBD] = chk;
        h
    }

    #[test]
    fn nds_identity_is_code_plus_header_crc16() {
        let h = nds_header(b"AMCE", 0x1A2B);
        assert!(!needs_full_hash(SystemId::Nds, &h));
        assert_eq!(rom_identity(SystemId::Nds, &h, None).unwrap(), "AMCE-1A2B");
        assert_eq!(
            rom_identity(SystemId::Nds, &nds_header(b"amce", 0x1A2B), None).unwrap(),
            "AMCE-1A2B"
        );
    }

    #[test]
    fn gba_identity_is_code_plus_complement() {
        let h = gba_header(b"POKEMON EMER", b"BPEE", 0x5A);
        assert_eq!(rom_identity(SystemId::Gba, &h, None).unwrap(), "BPEE-5A");
    }

    #[test]
    fn gba_display_name_is_file_stem_not_header_title() {
        let hack = gba_header(b"POKEMON FIRE", b"BPRE", 0x68);
        assert_eq!(
            display_name(SystemId::Gba, &hack, "roms/gba/Pokemon CHROME.gba"),
            "Pokemon CHROME"
        );
    }

    #[test]
    fn garbage_code_falls_back_to_crc32() {
        let h = nds_header(b"####", 0x1111);
        assert!(needs_full_hash(SystemId::Nds, &h));
        assert_eq!(rom_identity(SystemId::Nds, &h, None), None);
        assert_eq!(
            rom_identity(SystemId::Nds, &h, Some(0xDEADBEEF)).unwrap(),
            "DEADBEEF"
        );
        assert!(needs_full_hash(SystemId::Nds, &nds_header(b"0000", 0)));
        assert!(needs_full_hash(SystemId::Nds, &[0u8; 8]));
    }

    #[test]
    fn crc_systems_always_need_full_hash() {
        assert!(needs_full_hash(SystemId::Nes, &[]));
        assert_eq!(
            rom_identity_from_bytes(SystemId::Nes, b"123456789").unwrap(),
            "CBF43926"
        );
        let rom: alloc::vec::Vec<u8> = (0..5000u32).map(|i| i as u8).collect();
        assert_eq!(
            rom_identity_from_bytes(SystemId::Snes, &rom).unwrap(),
            format!("{:08X}", crc32(&rom))
        );
    }

    #[test]
    fn unsupported_system_has_no_identity() {
        assert_eq!(rom_identity(SystemId::Switch, &[], Some(1)), None);
        assert!(needs_full_hash(SystemId::Switch, &[]));
    }

    #[test]
    fn display_name_is_rom_file_stem() {
        assert_eq!(
            display_name(SystemId::Nes, &[], "roms/nes/Zelda (U).nes"),
            "Zelda (U)"
        );
    }
}
