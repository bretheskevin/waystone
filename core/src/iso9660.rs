use alloc::string::String;
use alloc::vec::Vec;

pub const SECTOR: u64 = 2048;
const SYSTEM_CNF_CAP: u64 = 4096;
const DIR_SCAN_CAP: u64 = 1024 * 1024;

/// First `FILE` line of a .cue sheet (the data track holding the ISO9660 image).
pub fn cue_first_data_file(cue_text: &str) -> Option<String> {
    for line in cue_text.lines() {
        let t = line.trim_start();
        // get(..4), not t[..4]: a multibyte char straddling byte 4 would panic.
        let Some(prefix) = t.get(..4) else { continue };
        if !prefix.eq_ignore_ascii_case("FILE") {
            continue;
        }
        let rest = t[4..].trim_start();
        if let Some(q) = rest.strip_prefix('"') {
            let end = q.find('"')?;
            if end > 0 {
                return Some(String::from(&q[..end]));
            }
            return None;
        }
        let end = rest.find(char::is_whitespace).unwrap_or(rest.len());
        if end > 0 {
            return Some(String::from(&rest[..end]));
        }
    }
    None
}

/// "<4 letters><5 digits>" in any spelling ("SLUS_012.34", "slus-01234") -> "SLUS-01234".
fn normalize_serial(raw: &str) -> Option<String> {
    let mut letters = String::new();
    let mut digits = String::new();
    for c in raw.trim().chars() {
        if c.is_ascii_alphabetic() && digits.is_empty() {
            letters.push(c.to_ascii_uppercase());
        } else if c.is_ascii_digit() {
            digits.push(c);
        }
    }
    if letters.len() != 4 || digits.len() != 5 {
        return None;
    }
    Some(format!("{}-{}", letters, digits))
}

fn read_all(read: &mut dyn FnMut(u64, &mut [u8]) -> usize, off: u64, len: u64) -> Option<Vec<u8>> {
    let mut buf = alloc::vec![0u8; len as usize];
    let mut got = 0usize;
    while got < buf.len() {
        let n = read(off + got as u64, &mut buf[got..]);
        if n == 0 {
            return None;
        }
        got += n;
    }
    Some(buf)
}

fn le32(rec: &[u8], off: usize) -> Option<u64> {
    let b = rec.get(off..off + 4)?;
    Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as u64)
}

fn find_dir_entry(dir: &[u8], want: &str) -> Option<(u64, u64)> {
    let mut off = 0usize;
    while off < dir.len() {
        let len = *dir.get(off)? as usize;
        if len == 0 {
            break; // zero padding to sector end
        }
        let rec = dir.get(off..off + len)?;
        let fi_len = *rec.get(32)? as usize;
        let fi = rec.get(33..33 + fi_len)?;
        // skip "." (0x00) and ".." (0x01)
        if !(fi_len == 1 && (fi[0] == 0 || fi[0] == 1)) {
            let name = core::str::from_utf8(fi).ok()?;
            let base = name.split(';').next().unwrap_or(name);
            if base.eq_ignore_ascii_case(want) {
                return Some((le32(rec, 2)?, le32(rec, 10)?));
            }
        }
        off += len;
    }
    None
}

/// PSX boot serial (e.g. "SLUS-01234") from the ISO9660 SYSTEM.CNF of the image
/// served by `read(offset, buf) -> bytes read`. None when the image is not
/// ISO9660 or has no parseable BOOT serial (caller falls back to sampled id).
pub fn psx_boot_serial(read: &mut dyn FnMut(u64, &mut [u8]) -> usize) -> Option<String> {
    let mut pvd = [0u8; 2048];
    if read(16 * SECTOR, &mut pvd) != 2048 {
        return None;
    }
    if pvd[0] != 1 || &pvd[1..6] != b"CD001" {
        return None;
    }
    let root = pvd.get(156..156 + 34)?;
    let root_lba = le32(root, 2)?;
    let root_len = le32(root, 10)?.min(DIR_SCAN_CAP);
    let dir = read_all(read, root_lba * SECTOR, root_len)?;
    let (cnf_lba, cnf_len) = find_dir_entry(&dir, "SYSTEM.CNF")?;
    if cnf_len == 0 || cnf_len > SYSTEM_CNF_CAP {
        return None;
    }
    let cnf = read_all(read, cnf_lba * SECTOR, cnf_len)?;
    let text = core::str::from_utf8(&cnf).ok()?;
    for line in text.lines() {
        let t = line.trim();
        // get(..4), not t[..4]: a multibyte char straddling byte 4 would panic.
        let Some(prefix) = t.get(..4) else { continue };
        if !prefix.eq_ignore_ascii_case("BOOT") {
            continue;
        }
        let tok = t[4..].trim_start_matches(['=', ' ', '\t']);
        let name = tok.rsplit(['\\', '/', ':']).next()?;
        let name = name.split(';').next()?;
        return normalize_serial(name);
    }
    None
}

/// Fallback identity when no boot serial exists (CHD, unparseable image):
/// size + CRC32 of first 4 MiB + last 4 MiB (computed by the shell via
/// ws_crc32_update). Stable per file; does NOT converge across different rips.
pub fn psx_fallback_id(file_size: u64, sample_crc32: u32) -> String {
    format!("{:X}-{:08X}", file_size, sample_crc32)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dir_record(lba: u32, data_len: u32, name: &[u8]) -> Vec<u8> {
        let fi_len = name.len();
        let rec_len = 33 + fi_len + usize::from(fi_len.is_multiple_of(2));
        let mut r = alloc::vec![0u8; rec_len];
        r[0] = rec_len as u8;
        r[2..6].copy_from_slice(&lba.to_le_bytes());
        r[6..10].copy_from_slice(&lba.to_be_bytes());
        r[10..14].copy_from_slice(&data_len.to_le_bytes());
        r[14..18].copy_from_slice(&data_len.to_be_bytes());
        r[28..30].copy_from_slice(&1u16.to_le_bytes());
        r[30..32].copy_from_slice(&1u16.to_be_bytes());
        r[32] = fi_len as u8;
        r[33..33 + fi_len].copy_from_slice(name);
        r
    }

    /// Minimal ISO9660 image: PVD at sector 16, root dir at sector 20,
    /// SYSTEM.CNF at sector 21.
    fn synthetic_iso(cnf: &str) -> Vec<u8> {
        let mut img = alloc::vec![0u8; 22 * 2048];
        let pvd = 16 * 2048;
        img[pvd] = 1;
        img[pvd + 1..pvd + 6].copy_from_slice(b"CD001");
        let root = dir_record(20, 2048, &[0]);
        img[pvd + 156..pvd + 156 + root.len()].copy_from_slice(&root);
        let mut root_dir = dir_record(20, 2048, &[0]);
        root_dir.extend(dir_record(20, 2048, &[1]));
        root_dir.extend(dir_record(21, cnf.len() as u32, b"SYSTEM.CNF;1"));
        img[20 * 2048..20 * 2048 + root_dir.len()].copy_from_slice(&root_dir);
        img[21 * 2048..21 * 2048 + cnf.len()].copy_from_slice(cnf.as_bytes());
        img
    }

    fn vec_reader(img: &[u8]) -> impl FnMut(u64, &mut [u8]) -> usize + '_ {
        move |off: u64, buf: &mut [u8]| {
            let start = off as usize;
            if start >= img.len() {
                return 0;
            }
            let n = buf.len().min(img.len() - start);
            buf[..n].copy_from_slice(&img[start..start + n]);
            n
        }
    }

    #[test]
    fn parses_boot_serial_from_synthetic_iso() {
        let img = synthetic_iso("BOOT = cdrom:\\SLUS_012.34;1\r\n");
        let mut rd = vec_reader(&img);
        assert_eq!(psx_boot_serial(&mut rd).as_deref(), Some("SLUS-01234"));
    }

    #[test]
    fn serial_spellings_normalize() {
        let img = synthetic_iso("BOOT=cdrom:\\scus-971.63;1\n");
        let mut rd = vec_reader(&img);
        assert_eq!(psx_boot_serial(&mut rd).as_deref(), Some("SCUS-97163"));
    }

    #[test]
    fn non_iso_and_missing_cnf_return_none() {
        let zero_img = alloc::vec![0u8; 22 * 2048];
        let mut zeros = vec_reader(&zero_img);
        assert_eq!(psx_boot_serial(&mut zeros), None);
        let img = synthetic_iso("NOTBOOT = nothing\n");
        let mut rd = vec_reader(&img);
        assert_eq!(psx_boot_serial(&mut rd), None);
        let bad = synthetic_iso("BOOT = cdrom:\\TOOLS_1.0;1\n"); // not 4+5
        let mut rd2 = vec_reader(&bad);
        assert_eq!(psx_boot_serial(&mut rd2), None);
    }

    #[test]
    fn cue_first_file_line_wins() {
        assert_eq!(
            cue_first_data_file(
                "FILE \"Crash Bandicoot.bin\" BINARY\n  TRACK 01 MODE2/2352\nFILE \"x.wav\" WAVE\n"
            )
            .as_deref(),
            Some("Crash Bandicoot.bin")
        );
        assert_eq!(
            cue_first_data_file("  FILE game.iso BINARY\n").as_deref(),
            Some("game.iso")
        );
        assert_eq!(cue_first_data_file("TRACK 01 MODE2/2352\n"), None);
        assert_eq!(cue_first_data_file(""), None);
    }

    #[test]
    fn fallback_id_is_size_plus_sample_crc() {
        assert_eq!(
            psx_fallback_id(0x260000000, 0xCBF43926),
            "260000000-CBF43926"
        );
        assert_eq!(psx_fallback_id(0, 0), "0-00000000");
    }

    #[test]
    fn non_ascii_lines_do_not_panic_or_match() {
        // "ééé" is 6 bytes: a naive t[..4] would slice mid-char and panic.
        assert_eq!(cue_first_data_file("ééé not a keyword\n"), None);
        assert_eq!(
            cue_first_data_file("ééé comment\nFILE \"game.bin\" BINARY\n").as_deref(),
            Some("game.bin")
        );
        let img = synthetic_iso("ééé remark\nBOOT = cdrom:\\SLUS_012.34;1\n");
        let mut rd = vec_reader(&img);
        assert_eq!(psx_boot_serial(&mut rd).as_deref(), Some("SLUS-01234"));
    }
}
