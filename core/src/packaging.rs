use alloc::string::String;
use alloc::vec::Vec;

use sha2::{Digest, Sha256};

use crate::crc32::crc32;
use crate::model::{ContentRef, FileRef, NormalizedSave, SaveEntry};

// STORE-only ZIP writer producing byte-exact output on every platform.
// Spec: PKZIP APPNOTE 6.3.9, features from version 2.0 only.
const DOS_DATE_1980: u16 = 0x0021;
const DOS_TIME_ZERO: u16 = 0x0000;

pub fn canonical_zip(files: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut sorted: Vec<(&str, &[u8])> = files
        .iter()
        .map(|(p, c)| (p.as_str(), c.as_slice()))
        .collect();
    sorted.sort_by_key(|(path, _)| *path);

    let mut buf = Vec::new();
    let mut central_entries: Vec<(usize, &str, &[u8], u32)> = Vec::new();

    for (path, content) in &sorted {
        let offset = buf.len();
        let crc = crc32(content);
        let size = content.len() as u32;
        let name_bytes = path.as_bytes();

        buf.extend_from_slice(&0x04034b50u32.to_le_bytes());
        buf.extend_from_slice(&20u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&DOS_TIME_ZERO.to_le_bytes());
        buf.extend_from_slice(&DOS_DATE_1980.to_le_bytes());
        buf.extend_from_slice(&crc.to_le_bytes());
        buf.extend_from_slice(&size.to_le_bytes());
        buf.extend_from_slice(&size.to_le_bytes());
        buf.extend_from_slice(&(name_bytes.len() as u16).to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(name_bytes);
        buf.extend_from_slice(content);

        central_entries.push((offset, path, content, crc));
    }

    let central_offset = buf.len();
    let count = central_entries.len() as u16;

    for (offset, path, content, crc) in &central_entries {
        let name_bytes = path.as_bytes();
        let size = content.len() as u32;

        buf.extend_from_slice(&0x02014b50u32.to_le_bytes());
        buf.extend_from_slice(&20u16.to_le_bytes());
        buf.extend_from_slice(&20u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&DOS_TIME_ZERO.to_le_bytes());
        buf.extend_from_slice(&DOS_DATE_1980.to_le_bytes());
        buf.extend_from_slice(&crc.to_le_bytes());
        buf.extend_from_slice(&size.to_le_bytes());
        buf.extend_from_slice(&size.to_le_bytes());
        buf.extend_from_slice(&(name_bytes.len() as u16).to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u16.to_le_bytes());
        buf.extend_from_slice(&0u32.to_le_bytes());
        buf.extend_from_slice(&(*offset as u32).to_le_bytes());
        buf.extend_from_slice(name_bytes);
    }

    let central_size = (buf.len() - central_offset) as u32;

    buf.extend_from_slice(&0x06054b50u32.to_le_bytes());
    buf.extend_from_slice(&0u16.to_le_bytes());
    buf.extend_from_slice(&0u16.to_le_bytes());
    buf.extend_from_slice(&count.to_le_bytes());
    buf.extend_from_slice(&count.to_le_bytes());
    buf.extend_from_slice(&central_size.to_le_bytes());
    buf.extend_from_slice(&(central_offset as u32).to_le_bytes());
    buf.extend_from_slice(&0u16.to_le_bytes());

    buf
}

#[derive(Debug)]
pub enum PackagingError {
    InvalidZip(String),
}

impl core::fmt::Display for PackagingError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::InvalidZip(msg) => write!(f, "invalid zip: {msg}"),
        }
    }
}

impl core::error::Error for PackagingError {}

fn read_u16_le(buf: &[u8], offset: usize) -> Result<u16, PackagingError> {
    let bytes: [u8; 2] = buf
        .get(offset..offset + 2)
        .and_then(|s| s.try_into().ok())
        .ok_or_else(|| PackagingError::InvalidZip("truncated field".into()))?;
    Ok(u16::from_le_bytes(bytes))
}

fn read_u32_le(buf: &[u8], offset: usize) -> Result<u32, PackagingError> {
    let bytes: [u8; 4] = buf
        .get(offset..offset + 4)
        .and_then(|s| s.try_into().ok())
        .ok_or_else(|| PackagingError::InvalidZip("truncated field".into()))?;
    Ok(u32::from_le_bytes(bytes))
}

pub fn unzip(zip_bytes: &[u8]) -> Result<Vec<(String, Vec<u8>)>, PackagingError> {
    let mut entries = Vec::new();
    let mut pos = 0;
    let len = zip_bytes.len();

    while pos + 4 <= len {
        let sig = read_u32_le(zip_bytes, pos)?;
        if sig != 0x04034b50 {
            break;
        }
        if pos + 30 > len {
            return Err(PackagingError::InvalidZip("truncated local header".into()));
        }
        let method = read_u16_le(zip_bytes, pos + 8)?;
        if method != 0 {
            return Err(PackagingError::InvalidZip(
                "only STORE (method 0) supported".into(),
            ));
        }
        let comp_size = read_u32_le(zip_bytes, pos + 18)? as usize;
        let name_len = read_u16_le(zip_bytes, pos + 26)? as usize;
        let extra_len = read_u16_le(zip_bytes, pos + 28)? as usize;

        let name_start = pos + 30;
        let name_end = name_start + name_len;
        if name_end > len {
            return Err(PackagingError::InvalidZip("truncated file name".into()));
        }
        let name = String::from_utf8(zip_bytes[name_start..name_end].to_vec())
            .map_err(|_| PackagingError::InvalidZip("invalid utf-8 file name".into()))?;

        let data_start = name_end
            .checked_add(extra_len)
            .ok_or_else(|| PackagingError::InvalidZip("offset overflow".into()))?;
        let data_end = data_start
            .checked_add(comp_size)
            .ok_or_else(|| PackagingError::InvalidZip("offset overflow".into()))?;
        if data_end > len {
            return Err(PackagingError::InvalidZip("truncated file data".into()));
        }
        let content = zip_bytes[data_start..data_end].to_vec();

        entries.push((name, content));
        pos = data_end;
    }

    entries.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(entries)
}

pub fn content_hash(zip_bytes: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(zip_bytes);
    hex::encode(hasher.finalize())
}

pub fn file_hash(content: &[u8]) -> String {
    let mut hasher = Sha256::new();
    hasher.update(content);
    hex::encode(hasher.finalize())
}

pub fn package(save: &NormalizedSave) -> (SaveEntry, Vec<u8>) {
    let zip_bytes = canonical_zip(&save.files);
    let hash = content_hash(&zip_bytes);

    let files: Vec<FileRef> = save
        .files
        .iter()
        .map(|(path, content)| FileRef {
            path: path.clone(),
            size: content.len() as u64,
            hash: file_hash(content),
        })
        .collect();

    let content = ContentRef {
        hash,
        size: zip_bytes.len() as u64,
        files,
    };

    let entry = SaveEntry {
        id: save.id.clone(),
        group_key: save.group_key.clone(),
        portable: save.portable,
        content,
        mtime: save.mtime.clone(),
    };

    (entry, zip_bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::string::ToString;

    #[test]
    fn canonical_zip_is_deterministic() {
        let files = vec![
            ("b.txt".to_string(), b"hello".to_vec()),
            ("a.txt".to_string(), b"world".to_vec()),
        ];
        let zip1 = canonical_zip(&files);
        let zip2 = canonical_zip(&files);
        assert_eq!(zip1, zip2, "same input must produce identical bytes");
    }

    #[test]
    fn canonical_zip_sorts_entries() {
        let files = vec![
            ("z.txt".to_string(), b"last".to_vec()),
            ("a.txt".to_string(), b"first".to_vec()),
        ];
        let zip_bytes = canonical_zip(&files);
        let entries = unzip(&zip_bytes).unwrap();
        assert_eq!(entries[0].0, "a.txt");
        assert_eq!(entries[1].0, "z.txt");
    }

    #[test]
    fn canonical_zip_round_trips() {
        let files = vec![
            ("saves/main.sav".to_string(), vec![0xDE, 0xAD, 0xBE, 0xEF]),
            ("meta.json".to_string(), b"{\"version\":1}".to_vec()),
        ];
        let zip_bytes = canonical_zip(&files);
        let restored = unzip(&zip_bytes).unwrap();
        let mut sorted_input = files.clone();
        sorted_input.sort_by(|a, b| a.0.cmp(&b.0));
        assert_eq!(restored, sorted_input);
    }

    #[test]
    fn content_hash_is_stable() {
        let files = vec![("test.bin".to_string(), vec![1, 2, 3, 4])];
        let zip_bytes = canonical_zip(&files);
        let hash1 = content_hash(&zip_bytes);
        let hash2 = content_hash(&zip_bytes);
        assert_eq!(hash1, hash2);
        assert_eq!(hash1.len(), 64);
    }

    #[test]
    fn package_produces_correct_content_ref() {
        let save = crate::model::NormalizedSave {
            id: crate::model::SaveId {
                source: "test".into(),
                system: crate::model::SystemId::Gba,
                game: crate::model::GameRef {
                    key: "TEST".into(),
                    display_name: "Test Game".into(),
                    confidence: crate::model::Confidence::Strong,
                    title_id: None,
                    serial: None,
                    rom_crc: None,
                },
                slot: "battery".into(),
                kind: crate::model::SaveKind::Battery,
            },
            group_key: "gba/TEST/battery".into(),
            portable: true,
            mtime: "2026-01-01T00:00:00Z".into(),
            files: vec![("save.sav".to_string(), vec![0xFF; 32])],
        };
        let (entry, zip_bytes) = package(&save);
        assert_eq!(entry.content.files.len(), 1);
        assert_eq!(entry.content.files[0].path, "save.sav");
        assert_eq!(entry.content.files[0].size, 32);
        assert_eq!(entry.content.size, zip_bytes.len() as u64);
        assert_eq!(entry.content.hash, content_hash(&zip_bytes));
    }

    #[test]
    fn empty_files_produce_valid_zip() {
        let files: Vec<(String, Vec<u8>)> = vec![];
        let zip_bytes = canonical_zip(&files);
        let restored = unzip(&zip_bytes).unwrap();
        assert!(restored.is_empty());
    }
}
