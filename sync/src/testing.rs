//! In-memory [`WebDav`] for tests across the workspace.
use crate::{Result, WebDav};
use std::collections::BTreeMap;
use std::sync::{Mutex, MutexGuard, PoisonError};

#[derive(Default)]
pub struct MemoryDav {
    store: Mutex<BTreeMap<String, Vec<u8>>>,
}

impl MemoryDav {
    pub fn new() -> Self {
        Self::default()
    }

    // A panicking test thread must not cascade into every later MemoryDav call.
    fn store(&self) -> MutexGuard<'_, BTreeMap<String, Vec<u8>>> {
        self.store.lock().unwrap_or_else(PoisonError::into_inner)
    }

    pub fn keys(&self) -> Vec<String> {
        self.store().keys().cloned().collect()
    }

    pub fn read(&self, path: &str) -> Option<Vec<u8>> {
        self.store().get(path).cloned()
    }
}

impl WebDav for MemoryDav {
    fn get(&self, path: &str) -> Result<Option<Vec<u8>>> {
        Ok(self.read(path))
    }

    fn put(&self, path: &str, body: Vec<u8>) -> Result<()> {
        self.store().insert(path.to_string(), body);
        Ok(())
    }

    fn exists(&self, path: &str) -> Result<bool> {
        Ok(self.store().contains_key(path))
    }

    fn propfind(&self, path: &str) -> Result<Vec<String>> {
        let prefix = if path.ends_with('/') {
            path.to_string()
        } else {
            format!("{path}/")
        };
        let mut out: Vec<String> = self
            .store()
            .keys()
            .filter(|k| k.starts_with(&prefix))
            .map(|k| {
                let rest = &k[prefix.len()..];
                match rest.find('/') {
                    Some(i) => format!("{prefix}{}", &rest[..=i]),
                    None => k.clone(),
                }
            })
            .collect();
        out.sort();
        out.dedup();
        Ok(out)
    }

    fn mkdir_p(&self, _path: &str) -> Result<()> {
        Ok(())
    }
}
