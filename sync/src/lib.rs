pub mod orchestration;

use std::fmt;

#[derive(Debug)]
pub enum SyncError {
    WebDav(String),
    Crypto(waystone_core::crypto::CryptoError),
    Packaging(waystone_core::packaging::PackagingError),
    Json(serde_json::Error),
    BlobNotFound(String),
}

impl fmt::Display for SyncError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::WebDav(msg) => write!(f, "webdav error: {msg}"),
            Self::Crypto(e) => write!(f, "crypto error: {e}"),
            Self::Packaging(e) => write!(f, "packaging error: {e}"),
            Self::Json(e) => write!(f, "json error: {e}"),
            Self::BlobNotFound(path) => write!(f, "blob not found: {path}"),
        }
    }
}

impl std::error::Error for SyncError {}

impl From<waystone_core::crypto::CryptoError> for SyncError {
    fn from(e: waystone_core::crypto::CryptoError) -> Self {
        Self::Crypto(e)
    }
}

impl From<waystone_core::packaging::PackagingError> for SyncError {
    fn from(e: waystone_core::packaging::PackagingError) -> Self {
        Self::Packaging(e)
    }
}

impl From<serde_json::Error> for SyncError {
    fn from(e: serde_json::Error) -> Self {
        Self::Json(e)
    }
}

pub type Result<T> = std::result::Result<T, SyncError>;

pub trait WebDav {
    fn get(&self, path: &str) -> Result<Option<Vec<u8>>>;
    fn put(&self, path: &str, body: Vec<u8>) -> Result<()>;
    fn exists(&self, path: &str) -> Result<bool>;
    fn propfind(&self, path: &str) -> Result<Vec<String>>;
    fn mkdir_p(&self, path: &str) -> Result<()>;
}

pub use orchestration::{
    HistoryEntry, PullOutcome, PushOutcome, fetch_blob, list_history, pull_one, push_one,
    read_remote_heads,
};
