#[derive(Debug, Clone, uniffi::Record)]
pub struct RawFileEntry {
    pub path: String,
    pub content: Vec<u8>,
}

#[derive(Debug, Clone, uniffi::Record)]
pub struct RawTree {
    pub files: Vec<RawFileEntry>,
}

#[derive(Debug, Clone, uniffi::Record)]
pub struct NormalizedSave {
    pub source: String,
    pub system: String,
    pub game_key: String,
    pub display_name: String,
    pub title_id: Option<String>,
    pub slot: String,
    pub kind: String,
    pub group_key: String,
    pub portable: bool,
    pub mtime: String,
    pub files: Vec<FileEntry>,
}

#[derive(Debug, Clone, uniffi::Record)]
pub struct FileEntry {
    pub path: String,
    pub content: Vec<u8>,
}

#[derive(Debug, Clone, uniffi::Record)]
pub struct DeviceHead {
    pub device_id: String,
    pub hash: String,
    pub mtime: String,
}

impl From<waystone_core::conflict::DeviceHead> for DeviceHead {
    fn from(h: waystone_core::conflict::DeviceHead) -> Self {
        Self {
            device_id: h.device_id,
            hash: h.hash,
            mtime: h.mtime,
        }
    }
}
