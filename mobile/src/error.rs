#[derive(Debug, uniffi::Error)]
pub enum WaystoneError {
    Crypto { msg: String },
    Packaging { msg: String },
    WebDav { msg: String },
    Json { msg: String },
    BlobNotFound { path: String },
}

impl std::fmt::Display for WaystoneError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Crypto { msg } => write!(f, "crypto: {msg}"),
            Self::Packaging { msg } => write!(f, "packaging: {msg}"),
            Self::WebDav { msg } => write!(f, "webdav: {msg}"),
            Self::Json { msg } => write!(f, "json: {msg}"),
            Self::BlobNotFound { path } => write!(f, "blob not found: {path}"),
        }
    }
}

impl From<waystone_sync::SyncError> for WaystoneError {
    fn from(e: waystone_sync::SyncError) -> Self {
        match e {
            waystone_sync::SyncError::Crypto(c) => Self::Crypto { msg: c.to_string() },
            waystone_sync::SyncError::Packaging(p) => Self::Packaging { msg: p.to_string() },
            waystone_sync::SyncError::WebDav(w) => Self::WebDav { msg: w },
            waystone_sync::SyncError::Json(j) => Self::Json { msg: j.to_string() },
            waystone_sync::SyncError::BlobNotFound(p) => Self::BlobNotFound { path: p },
        }
    }
}

impl From<waystone_core::crypto::CryptoError> for WaystoneError {
    fn from(e: waystone_core::crypto::CryptoError) -> Self {
        Self::Crypto { msg: e.to_string() }
    }
}
