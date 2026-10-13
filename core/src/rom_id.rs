use alloc::string::{String, ToString};

use crate::crc32::crc32;
use crate::model::SystemId;
use crate::rom_systems::{IdScheme, base_name, file_stem, rom_system};

pub const ROM_HEADER_LEN: usize = 0x200;

const NDS_CODE: usize = 0x0C;
const NDS_HEADER_CRC16: usize = 0x15E;
const GBA_CODE: usize = 0xAC;
const GBA_COMPLEMENT: usize = 0xBD;
const N64_CODE: usize = 0x3B;
const N64_REGION: usize = 0x3E;
const N64_VERSION: usize = 0x3F;

// N64 ROMs start with magic 0x80371240 big-endian (.z64). .v64 is 16-bit
// byteswapped (37 80 40 12), .n64 is 32-bit little-endian (40 12 37 80).
// Normalize to big-endian before reading the header; unknown magic -> fallback.
fn n64_normalize<'h>(header: &'h [u8], buf: &'h mut [u8; ROM_HEADER_LEN]) -> Option<&'h [u8]> {
    let n = header.len().min(ROM_HEADER_LEN);
    match header.get(0..4)? {
        [0x80, 0x37, 0x12, 0x40] => Some(&header[..n]),
        [0x37, 0x80, 0x40, 0x12] => {
            for i in 0..n / 2 {
                buf[2 * i] = header[2 * i + 1];
                buf[2 * i + 1] = header[2 * i];
            }
            Some(&buf[..n])
        }
        [0x40, 0x12, 0x37, 0x80] => {
            for i in 0..n / 4 {
                for j in 0..4 {
                    buf[4 * i + j] = header[4 * i + 3 - j];
                }
            }
            Some(&buf[..n])
        }
        _ => None,
    }
}

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
        IdScheme::N64Header => {
            let mut buf = [0u8; ROM_HEADER_LEN];
            let h = n64_normalize(header, &mut buf)?;
            let code = game_code(h, N64_CODE)?;
            Some(format!(
                "{}-{:X}{:X}",
                code,
                h.get(N64_REGION)?,
                h.get(N64_VERSION)?
            ))
        }
        IdScheme::PsxSerial => None, // ISO9660 walk; see iso9660.rs / ws_rom_psx_identity
    }
}

pub fn needs_full_hash(system: SystemId, header: &[u8]) -> bool {
    match rom_system(system) {
        // PSX identity is serial-or-sampled via ws_rom_psx_identity, never a full-file hash.
        Some(rs) if rs.id_scheme == IdScheme::PsxSerial => false,
        Some(rs) => header_identity(rs.id_scheme, header).is_none(),
        None => true,
    }
}

pub fn rom_identity(system: SystemId, header: &[u8], full_crc32: Option<u32>) -> Option<String> {
    let rs = rom_system(system)?;
    if rs.id_scheme == IdScheme::PsxSerial {
        return None; // shells must use the dedicated PSX FFI (serial or size+sample id)
    }
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

    fn n64_header_be(code: &[u8; 4], region: u8, version: u8) -> alloc::vec::Vec<u8> {
        let mut h = vec![0u8; ROM_HEADER_LEN];
        h[0..4].copy_from_slice(&[0x80, 0x37, 0x12, 0x40]);
        h[0x3B..0x3F].copy_from_slice(code);
        h[0x3E] = region;
        h[0x3F] = version;
        h
    }

    fn swap16(h: &[u8]) -> alloc::vec::Vec<u8> {
        let mut v = h.to_vec();
        for i in 0..v.len() / 2 {
            v.swap(2 * i, 2 * i + 1);
        }
        v
    }

    fn swap32(h: &[u8]) -> alloc::vec::Vec<u8> {
        let mut v = h.to_vec();
        for i in 0..v.len() / 4 {
            v.swap(4 * i, 4 * i + 3);
            v.swap(4 * i + 1, 4 * i + 2);
        }
        v
    }

    #[test]
    fn n64_identity_is_cart_code_region_version() {
        let h = n64_header_be(b"NSME", 0x45, 0x00); // Super Mario 64 (E), v0
        assert!(!needs_full_hash(SystemId::N64, &h));
        assert_eq!(rom_identity(SystemId::N64, &h, None).unwrap(), "NSME-450");
    }

    #[test]
    fn n64_byteswapped_inputs_normalize_to_same_id() {
        let be = n64_header_be(b"NZLE", 0x45, 0x01);
        assert_eq!(
            rom_identity(SystemId::N64, &swap16(&be), None).unwrap(),
            "NZLE-451"
        );
        assert_eq!(
            rom_identity(SystemId::N64, &swap32(&be), None).unwrap(),
            "NZLE-451"
        );
    }

    #[test]
    fn n64_unknown_magic_falls_back_to_crc32() {
        let h = n64_header_be(b"NSME", 0x45, 0x00);
        let mut bad = h.clone();
        bad[0] = 0xFF;
        assert!(needs_full_hash(SystemId::N64, &bad));
        assert_eq!(
            rom_identity(SystemId::N64, &bad, Some(0xCBF43926)).unwrap(),
            "CBF43926"
        );
    }

    #[test]
    fn psx_identity_never_uses_header_or_full_crc() {
        assert!(!needs_full_hash(SystemId::Psx, &[])); // sampled path, never full hash
        assert_eq!(rom_identity(SystemId::Psx, &[], Some(1)), None); // dedicated FFI only
    }
}
