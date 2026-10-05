use alloc::string::String;
use alloc::vec::Vec;

/// A decoded file tree: ordered (path, bytes).
pub type FileTree = Vec<(String, Vec<u8>)>;
/// A decoded save list: per save, (metadata JSON bytes, its file tree).
pub type SaveList = Vec<(Vec<u8>, FileTree)>;

#[derive(Debug)]
pub enum WireError {
    Truncated,
    BadUtf8,
    TooLarge,
}

impl core::fmt::Display for WireError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            WireError::Truncated => write!(f, "wire: truncated buffer"),
            WireError::BadUtf8 => write!(f, "wire: invalid utf-8 path"),
            WireError::TooLarge => write!(f, "wire: length exceeds buffer"),
        }
    }
}

impl core::error::Error for WireError {}

// ---- encode ----

pub fn encode_file_tree(files: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&(files.len() as u32).to_le_bytes());
    for (path, data) in files {
        let pb = path.as_bytes();
        out.extend_from_slice(&(pb.len() as u32).to_le_bytes());
        out.extend_from_slice(pb);
        out.extend_from_slice(&(data.len() as u64).to_le_bytes());
        out.extend_from_slice(data);
    }
    out
}

/// Each save = (metadata JSON bytes, already-encoded WsFileTree bytes).
pub fn encode_save_list(saves: &[(Vec<u8>, Vec<u8>)]) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&(saves.len() as u32).to_le_bytes());
    for (meta, files) in saves {
        out.extend_from_slice(&(meta.len() as u32).to_le_bytes());
        out.extend_from_slice(meta);
        out.extend_from_slice(files); // files is a full WsFileTree buffer
    }
    out
}

// ---- decode (bounds-checked cursor) ----

struct Cursor<'a> {
    buf: &'a [u8],
    pos: usize,
}

impl<'a> Cursor<'a> {
    fn new(buf: &'a [u8]) -> Self {
        Cursor { buf, pos: 0 }
    }

    fn take(&mut self, n: u64) -> Result<&'a [u8], WireError> {
        let remaining = (self.buf.len() - self.pos) as u64;
        if n > remaining {
            return Err(WireError::TooLarge);
        }
        let n = n as usize; // safe: n <= remaining <= buf.len() <= usize::MAX
        let start = self.pos;
        self.pos += n;
        Ok(&self.buf[start..start + n])
    }

    fn u32(&mut self) -> Result<u32, WireError> {
        let b = self.take(4)?;
        Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }

    fn u64(&mut self) -> Result<u64, WireError> {
        let b = self.take(8)?;
        Ok(u64::from_le_bytes([
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
        ]))
    }
}

fn decode_file_tree_at(c: &mut Cursor<'_>) -> Result<FileTree, WireError> {
    let count = c.u32()?;
    let mut files = Vec::new();
    for _ in 0..count {
        let path_len = c.u32()? as u64;
        let path = c.take(path_len)?;
        let path = String::from(core::str::from_utf8(path).map_err(|_| WireError::BadUtf8)?);
        let data_len = c.u64()?;
        let data = c.take(data_len)?.to_vec();
        files.push((path, data));
    }
    Ok(files)
}

pub fn decode_file_tree(buf: &[u8]) -> Result<FileTree, WireError> {
    decode_file_tree_at(&mut Cursor::new(buf))
}

/// Returns (meta_json_bytes, decoded files) per save.
pub fn decode_save_list(buf: &[u8]) -> Result<SaveList, WireError> {
    let mut c = Cursor::new(buf);
    let count = c.u32()?;
    let mut out = Vec::new();
    for _ in 0..count {
        let meta_len = c.u32()? as u64;
        let meta = c.take(meta_len)?.to_vec();
        let files = decode_file_tree_at(&mut c)?;
        out.push((meta, files));
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::string::ToString;
    use alloc::vec;
    use alloc::vec::Vec;

    // Frozen golden — MUST match shell-common/tests/test_file_tree.cpp exactly.
    const FILE_TREE_GOLDEN: &[u8] = &[
        0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x61, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xAA, 0x02, 0x00, 0x00, 0x00, 0x62, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
    ];
    const SAVE_LIST_GOLDEN: &[u8] = &[
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x7B, 0x7D, 0x00, 0x00, 0x00, 0x00,
    ];

    #[test]
    fn file_tree_golden_encodes_exact_bytes() {
        let files = vec![
            ("a".to_string(), vec![0xAAu8]),
            ("bb".to_string(), Vec::new()),
        ];
        assert_eq!(encode_file_tree(&files), FILE_TREE_GOLDEN);
    }

    #[test]
    fn file_tree_round_trips() {
        let files = vec![
            ("saves/main.sav".to_string(), vec![0xDE, 0xAD, 0xBE, 0xEF]),
            ("empty".to_string(), Vec::new()),
        ];
        let buf = encode_file_tree(&files);
        assert_eq!(decode_file_tree(&buf).unwrap(), files);
    }

    #[test]
    fn save_list_golden_encodes_exact_bytes() {
        let saves = vec![(b"{}".to_vec(), encode_file_tree(&[]))];
        assert_eq!(encode_save_list(&saves), SAVE_LIST_GOLDEN);
    }

    #[test]
    fn save_list_round_trips() {
        let files = vec![("data.sav".to_string(), vec![1u8, 2, 3])];
        let saves = vec![(b"{\"group_key\":\"x\"}".to_vec(), encode_file_tree(&files))];
        let buf = encode_save_list(&saves);
        let decoded = decode_save_list(&buf).unwrap();
        assert_eq!(decoded.len(), 1);
        assert_eq!(decoded[0].0, b"{\"group_key\":\"x\"}");
        assert_eq!(decoded[0].1, files);
    }

    #[test]
    fn decode_rejects_truncation() {
        assert!(decode_file_tree(&[0x01, 0x00, 0x00, 0x00]).is_err()); // claims 1 entry, no body
        assert!(decode_file_tree(&[0x00, 0x00]).is_err()); // truncated count
    }

    #[test]
    fn decode_rejects_overflow_len() {
        // count=1, path_len=0, data_len=u64::MAX
        let buf = [
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF,
        ];
        assert!(decode_file_tree(&buf).is_err());
    }
}
