use crate::error::WaystoneError;
use std::sync::Arc;

#[uniffi::export(with_foreign)]
pub trait WebDav: Send + Sync {
    fn get(&self, path: String) -> Result<Option<Vec<u8>>, WaystoneError>;
    fn put(&self, path: String, body: Vec<u8>) -> Result<(), WaystoneError>;
    fn exists(&self, path: String) -> Result<bool, WaystoneError>;
    fn propfind(&self, path: String) -> Result<Vec<String>, WaystoneError>;
    fn mkdir_p(&self, path: String) -> Result<(), WaystoneError>;
}

pub(crate) struct WebDavBridge {
    pub inner: Arc<dyn WebDav>,
}

impl waystone_sync::WebDav for WebDavBridge {
    fn get(&self, path: &str) -> waystone_sync::Result<Option<Vec<u8>>> {
        self.inner
            .get(path.to_string())
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))
    }

    fn put(&self, path: &str, body: Vec<u8>) -> waystone_sync::Result<()> {
        self.inner
            .put(path.to_string(), body)
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))
    }

    fn exists(&self, path: &str) -> waystone_sync::Result<bool> {
        self.inner
            .exists(path.to_string())
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))
    }

    fn propfind(&self, path: &str) -> waystone_sync::Result<Vec<String>> {
        self.inner
            .propfind(path.to_string())
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))
    }

    fn mkdir_p(&self, path: &str) -> waystone_sync::Result<()> {
        self.inner
            .mkdir_p(path.to_string())
            .map_err(|e| waystone_sync::SyncError::WebDav(e.to_string()))
    }
}
