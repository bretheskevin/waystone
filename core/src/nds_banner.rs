use alloc::string::String;
use alloc::vec::Vec;

pub const NDS_BANNER_LEN: usize = 0x840;
pub const NDS_ICON_SIDE: usize = 32;
const ICON_OFF: usize = 0x20;
const PALETTE_OFF: usize = 0x220;
const TITLE_EN_OFF: usize = 0x340;
const TITLE_UNITS: usize = 0x80;

/// NDS header 0x68 banner offset; None when absent (0) or the header is too short.
pub fn nds_banner_offset(header: &[u8]) -> Option<u32> {
    let b = header.get(0x68..0x6C)?;
    let off = u32::from_le_bytes([b[0], b[1], b[2], b[3]]);
    (off != 0).then_some(off)
}

fn expand5(v: u16) -> u8 {
    ((v << 3) | (v >> 2)) as u8
}

/// 32x32 icon as RGBA8888 row-major; palette index 0 is fully transparent (0,0,0,0).
pub fn nds_banner_icon_rgba(banner: &[u8]) -> Option<Vec<u8>> {
    if banner.len() < PALETTE_OFF + 0x20 {
        return None;
    }
    let mut pal = [[0u8; 4]; 16];
    for (i, p) in pal.iter_mut().enumerate().skip(1) {
        let c = u16::from_le_bytes([banner[PALETTE_OFF + 2 * i], banner[PALETTE_OFF + 2 * i + 1]]);
        *p = [
            expand5(c & 0x1F),
            expand5((c >> 5) & 0x1F),
            expand5((c >> 10) & 0x1F),
            255,
        ];
    }
    let mut out = vec![0u8; NDS_ICON_SIDE * NDS_ICON_SIDE * 4];
    for y in 0..NDS_ICON_SIDE {
        for x in 0..NDS_ICON_SIDE {
            let tile = (y / 8) * 4 + x / 8;
            let byte = banner[ICON_OFF + tile * 32 + (y % 8) * 4 + (x % 8) / 2];
            let idx = if x % 2 == 0 { byte & 0x0F } else { byte >> 4 };
            let o = (y * NDS_ICON_SIDE + x) * 4;
            out[o..o + 4].copy_from_slice(&pal[idx as usize]);
        }
    }
    Some(out)
}

/// English banner title (0x340): every non-empty line but the last (publisher) joined by ' '.
pub fn nds_banner_title(banner: &[u8]) -> String {
    let Some(raw) = banner.get(TITLE_EN_OFF..TITLE_EN_OFF + TITLE_UNITS * 2) else {
        return String::new();
    };
    let units = raw
        .as_chunks::<2>()
        .0
        .iter()
        .map(|c| u16::from_le_bytes(*c))
        .take_while(|&u| u != 0);
    let text: String = char::decode_utf16(units)
        .map(|r| r.unwrap_or(char::REPLACEMENT_CHARACTER))
        .collect();
    let lines: Vec<&str> = text.split('\n').filter(|l| !l.is_empty()).collect();
    let keep = if lines.len() >= 2 {
        lines.len() - 1
    } else {
        lines.len()
    };
    lines[..keep].join(" ")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn banner() -> Vec<u8> {
        let mut b = vec![0u8; NDS_BANNER_LEN];
        // palette: 0 = (ignored) white, 1 = pure red, 2 = pure green
        b[0x220..0x222].copy_from_slice(&0x7FFFu16.to_le_bytes());
        b[0x222..0x224].copy_from_slice(&0x001Fu16.to_le_bytes());
        b[0x224..0x226].copy_from_slice(&0x03E0u16.to_le_bytes());
        b[0x20] = 0x21; // tile 0 row 0: x=0 -> idx 1, x=1 -> idx 2
        b[0x20 + 32] = 0x01; // tile 1 (x=8, y=0) -> idx 1
        b[0x20 + 4 * 32] = 0x20; // tile 4 (x=0, y=8): x=0 -> idx 0, x=1 -> idx 2
        let title: Vec<u16> = "Mario Kart DS\nNintendo".encode_utf16().collect();
        for (k, u) in title.iter().enumerate() {
            b[0x340 + 2 * k..0x342 + 2 * k].copy_from_slice(&u.to_le_bytes());
        }
        b
    }

    fn px(icon: &[u8], x: usize, y: usize) -> [u8; 4] {
        let o = (y * NDS_ICON_SIDE + x) * 4;
        [icon[o], icon[o + 1], icon[o + 2], icon[o + 3]]
    }

    #[test]
    fn offset_reads_header_0x68_le() {
        let mut h = vec![0u8; 0x200];
        h[0x68..0x6C].copy_from_slice(&0x0012_3400u32.to_le_bytes());
        assert_eq!(nds_banner_offset(&h), Some(0x0012_3400));
        assert_eq!(nds_banner_offset(&h[..0x6B]), None);
        assert_eq!(nds_banner_offset(&vec![0u8; 0x200]), None);
    }

    #[test]
    fn icon_decodes_tiled_4bpp_with_transparent_index_0() {
        let icon = nds_banner_icon_rgba(&banner()).unwrap();
        assert_eq!(icon.len(), NDS_ICON_SIDE * NDS_ICON_SIDE * 4);
        assert_eq!(px(&icon, 0, 0), [255, 0, 0, 255]);
        assert_eq!(px(&icon, 1, 0), [0, 255, 0, 255]);
        assert_eq!(px(&icon, 8, 0), [255, 0, 0, 255]);
        assert_eq!(px(&icon, 0, 8), [0, 0, 0, 0]);
        assert_eq!(px(&icon, 1, 8), [0, 255, 0, 255]);
        assert_eq!(px(&icon, 31, 31), [0, 0, 0, 0]);
    }

    #[test]
    fn icon_none_when_banner_too_short() {
        assert!(nds_banner_icon_rgba(&banner()[..0x23F]).is_none());
    }

    #[test]
    fn title_drops_publisher_line() {
        assert_eq!(nds_banner_title(&banner()), "Mario Kart DS");
        let mut b = banner();
        b[0x340..0x440].fill(0);
        for (k, u) in "A\nB\nPub".encode_utf16().enumerate() {
            b[0x340 + 2 * k..0x342 + 2 * k].copy_from_slice(&u.to_le_bytes());
        }
        assert_eq!(nds_banner_title(&b), "A B");
        b[0x340..0x440].fill(0);
        for (k, u) in "Solo".encode_utf16().enumerate() {
            b[0x340 + 2 * k..0x342 + 2 * k].copy_from_slice(&u.to_le_bytes());
        }
        assert_eq!(nds_banner_title(&b), "Solo");
        assert_eq!(nds_banner_title(&b[..0x300]), "");
    }
}
