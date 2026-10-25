//! DeSmuME `.dsv` battery saves: raw data + footer (desmume/src/mc.cpp `BackupDevice::ensure`).
use alloc::vec::Vec;

pub const FOOTER_TXT: &[u8] =
    b"|<--Snip above here to create a raw sav by excluding this DeSmuME savedata footer:";
pub const COOKIE: &[u8] = b"|-DESMUME SAVE-|";
const FIELDS: usize = 6;
pub const FOOTER_LEN: usize = FOOTER_TXT.len() + FIELDS * 4 + COOKIE.len();

fn field(footer: &[u8], i: usize) -> u32 {
    let at = FOOTER_TXT.len() + 4 * i;
    u32::from_le_bytes([footer[at], footer[at + 1], footer[at + 2], footer[at + 3]])
}

/// (data region, footer); footer is None unless the file ends with a version-0 DeSmuME footer.
pub fn dsv_split(bytes: &[u8]) -> (&[u8], Option<&[u8]>) {
    if bytes.len() < FOOTER_LEN || !bytes.ends_with(COOKIE) {
        return (bytes, None);
    }
    let start = bytes.len() - FOOTER_LEN;
    let footer = &bytes[start..];
    if !footer.starts_with(FOOTER_TXT) || field(footer, 5) != 0 {
        return (bytes, None);
    }
    (&bytes[..start], Some(footer))
}

/// Bytes [dsv_decode] keeps from a `data_len` data region: the footer's mem size when smaller.
fn decoded_len(footer: &[u8], data_len: usize) -> usize {
    let mem = field(footer, 4) as usize;
    if mem > 0 && mem <= data_len {
        mem
    } else {
        data_len
    }
}

/// Canonical battery bytes: data region trimmed to the footer's mem size when smaller.
pub fn dsv_decode(bytes: &[u8]) -> Vec<u8> {
    let (data, footer) = dsv_split(bytes);
    let len = footer.map_or(data.len(), |f| decoded_len(f, data.len()));
    data[..len].to_vec()
}

// Footer `type` is the index into DeSmuME's `save_types[]`; 0 = unknown size (DeSmuME re-detects).
fn synth_type(len: usize) -> u32 {
    match len {
        512 => 1,
        8192 => 2,
        65536 => 3,
        32768 => 4,
        262144 => 5,
        524288 => 6,
        1048576 => 7,
        2097152 => 8,
        4194304 => 9,
        8388608 => 10,
        16777216 => 11,
        33554432 => 12,
        67108864 => 13,
        _ => 0,
    }
}

// mc.cpp addr_size_for_old_save_size
fn addr_size_for(len: usize) -> u32 {
    match len {
        0..=512 => 1,
        513..=65536 => 2,
        _ => 3,
    }
}

fn footer_bytes(addr: u32, pad: u32, kind: u32, addr_size: u32, mem: u32) -> Vec<u8> {
    let mut v = Vec::with_capacity(FOOTER_LEN);
    v.extend_from_slice(FOOTER_TXT);
    for f in [addr, pad, kind, addr_size, mem, 0] {
        v.extend_from_slice(&f.to_le_bytes());
    }
    v.extend_from_slice(COOKIE);
    v
}

/// Native `.dsv` from canonical bytes. Reuses `existing_footer`'s layout when its region can hold
/// `raw` (padding with 0xFF up to padSize, like DeSmuME) and still decodes back to exactly `raw`
/// (else the content hash would drift); otherwise synthesizes a loadable footer.
pub fn dsv_encode(raw: &[u8], existing_footer: Option<&[u8]>) -> Vec<u8> {
    let mut out = raw.to_vec();
    let len = raw.len() as u32;
    let reusable = existing_footer.filter(|f| {
        let pad = field(f, 1) as usize;
        pad >= raw.len() && decoded_len(f, pad) == raw.len()
    });
    match reusable {
        Some(f) => {
            out.resize(field(f, 1) as usize, 0xFF);
            out.extend_from_slice(f);
        }
        None => out.extend_from_slice(&footer_bytes(
            len,
            len,
            synth_type(raw.len()),
            addr_size_for(raw.len()),
            len,
        )),
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    /// DeSmuME-style file: [raw] padded with 0xFF to [pad], footer with mem size [mem].
    fn desmume_file(raw: &[u8], pad: usize, mem: usize) -> Vec<u8> {
        let mut v = raw.to_vec();
        v.resize(pad, 0xFF);
        v.extend_from_slice(FOOTER_TXT);
        for f in [raw.len() as u32, pad as u32, 2, 2, mem as u32, 0] {
            v.extend_from_slice(&f.to_le_bytes());
        }
        v.extend_from_slice(COOKIE);
        v
    }

    fn footer_of(bytes: &[u8]) -> &[u8] {
        dsv_split(bytes).1.expect("DeSmuME footer")
    }

    #[test]
    fn split_strips_a_desmume_footer() {
        let raw = vec![7u8; 8192];
        let file = desmume_file(&raw, 8192, 8192);
        let (data, footer) = dsv_split(&file);
        assert_eq!(data, &raw[..]);
        assert_eq!(footer.map(<[u8]>::len), Some(FOOTER_LEN));
    }

    #[test]
    fn split_without_footer_returns_input() {
        let raw = vec![1u8; 512];
        assert_eq!(dsv_split(&raw), (&raw[..], None));
    }

    #[test]
    fn decode_trims_padding_to_mem_size() {
        let raw = vec![3u8; 8192];
        assert_eq!(dsv_decode(&desmume_file(&raw, 65536, 8192)), raw);
        assert_eq!(dsv_decode(&desmume_file(&raw, 8192, 0)), raw);
    }

    #[test]
    fn encode_preserves_existing_footer_when_sizes_match() {
        let raw = vec![9u8; 8192];
        let existing = desmume_file(&[0u8; 8192], 8192, 8192);
        let out = dsv_encode(&raw, Some(footer_of(&existing)));
        assert_eq!(&out[..8192], &raw[..]);
        assert_eq!(&out[8192..], footer_of(&existing));
    }

    #[test]
    fn encode_pads_into_a_larger_existing_region() {
        let existing = desmume_file(&[0u8; 8192], 65536, 8192);
        let out = dsv_encode(&[5u8; 8192], Some(footer_of(&existing)));
        assert_eq!(out.len(), 65536 + FOOTER_LEN);
        assert!(out[8192..65536].iter().all(|b| *b == 0xFF));
        assert_eq!(dsv_decode(&out), vec![5u8; 8192]);
    }

    #[test]
    fn encode_never_reuses_a_footer_that_would_change_the_decoded_bytes() {
        let larger_mem = desmume_file(&[0u8; 65536], 65536, 65536);
        let out = dsv_encode(&[5u8; 8192], Some(footer_of(&larger_mem)));
        assert_eq!(dsv_decode(&out), vec![5u8; 8192]);
        assert_eq!(field(footer_of(&out), 4), 8192);
    }

    #[test]
    fn encode_synthesizes_a_loadable_footer() {
        let raw = vec![4u8; 512];
        let out = dsv_encode(&raw, None);
        assert_eq!(dsv_split(&out).0, &raw[..]);
        let f = footer_of(&out);
        assert_eq!(
            [0, 1, 2, 3, 4, 5].map(|i| field(f, i)),
            [512, 512, 1, 1, 512, 0]
        );
        assert_eq!(dsv_decode(&out), raw);
    }

    #[test]
    fn synthesized_type_is_the_desmume_save_types_index() {
        let synth = |len: usize, i: usize| field(footer_of(&dsv_encode(&vec![0u8; len], None)), i);
        let ty = |len: usize| synth(len, 2);
        let addr = |len: usize| synth(len, 3);
        assert_eq!(
            [512, 8192, 65536, 32768, 262144, 524288, 1048576].map(ty),
            [1, 2, 3, 4, 5, 6, 7]
        );
        assert_eq!(ty(131072), 0);
        assert_eq!(
            [512, 8192, 32768, 65536, 131072, 524288].map(addr),
            [1, 2, 2, 2, 3, 3]
        );
    }
}
