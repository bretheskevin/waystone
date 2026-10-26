#[derive(Debug, uniffi::Error)]
pub enum WaystoneError {
    Crypto {
        msg: String,
    },
    Packaging {
        msg: String,
    },
    WebDav {
        msg: String,
    },
    Json {
        msg: String,
    },
    BlobNotFound {
        path: String,
    },
    InvalidSystem {
        system: String,
    },
    InvalidFlavor {
        flavor: String,
    },
    Restore {
        msg: String,
    },
    /// A foreign callback threw something other than a WaystoneException.
    Callback {
        msg: String,
    },
}

impl std::fmt::Display for WaystoneError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Crypto { msg } => write!(f, "crypto: {msg}"),
            Self::Packaging { msg } => write!(f, "packaging: {msg}"),
            Self::WebDav { msg } => write!(f, "webdav: {msg}"),
            Self::Json { msg } => write!(f, "json: {msg}"),
            Self::BlobNotFound { path } => write!(f, "blob not found: {path}"),
            Self::InvalidSystem { system } => write!(f, "unknown system: {system}"),
            Self::InvalidFlavor { flavor } => write!(f, "unknown save flavor: {flavor}"),
            Self::Restore { msg } => write!(f, "restore: {msg}"),
            Self::Callback { msg } => write!(f, "callback failed: {msg}"),
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

impl From<waystone_sync::engine::RestoreError> for WaystoneError {
    fn from(e: waystone_sync::engine::RestoreError) -> Self {
        match e {
            waystone_sync::engine::RestoreError::Fetch(sync) => sync.into(),
            waystone_sync::engine::RestoreError::Write(msg) => Self::Restore { msg },
        }
    }
}

// A foreign callback that throws something other than WaystoneException would otherwise
// panic in Rust and abort the whole call instead of failing the one save.
impl From<uniffi::UnexpectedUniFFICallbackError> for WaystoneError {
    fn from(e: uniffi::UnexpectedUniFFICallbackError) -> Self {
        Self::Callback { msg: e.reason }
    }
}

impl From<waystone_core::crypto::CryptoError> for WaystoneError {
    fn from(e: waystone_core::crypto::CryptoError) -> Self {
        Self::Crypto { msg: e.to_string() }
    }
}
