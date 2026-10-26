use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

pub const LOG_MAX_BYTES: u64 = 256 * 1024;
pub const LOG_FILE_NAME: &str = "waystone.log";

pub fn rotate_if_needed(path: &Path, max: u64) {
    let Ok(meta) = std::fs::metadata(path) else {
        return;
    };
    if meta.len() <= max {
        return;
    }
    let old = PathBuf::from(format!("{}.old", path.display()));
    let _ = std::fs::remove_file(&old);
    if std::fs::rename(path, &old).is_err() {
        let _ = std::fs::remove_file(path);
    }
}

pub struct RotatingFile {
    path: PathBuf,
    max: u64,
    file: File,
    written: u64,
}

impl RotatingFile {
    pub fn open(path: &Path, max: u64) -> std::io::Result<Self> {
        rotate_if_needed(path, max);
        let file = OpenOptions::new().create(true).append(true).open(path)?;
        let written = file.metadata()?.len();
        Ok(Self {
            path: path.to_path_buf(),
            max,
            file,
            written,
        })
    }

    pub fn write_line(&mut self, line: &str) -> std::io::Result<()> {
        if self.written > self.max {
            rotate_if_needed(&self.path, self.max);
            self.file = OpenOptions::new()
                .create(true)
                .append(true)
                .open(&self.path)?;
            self.written = self.file.metadata()?.len();
        }
        writeln!(self.file, "{line}")?;
        self.written += line.len() as u64 + 1;
        Ok(())
    }
}

struct FileLogger(Mutex<RotatingFile>);

impl log::Log for FileLogger {
    fn enabled(&self, m: &log::Metadata) -> bool {
        m.level() <= log::Level::Info
    }

    fn log(&self, r: &log::Record) {
        if !self.enabled(r.metadata()) {
            return;
        }
        let line = format!(
            "{} {:<5} {}",
            chrono::Utc::now().format("%Y-%m-%dT%H:%M:%S%.3fZ"),
            r.level(),
            r.args()
        );
        if let Ok(mut f) = self.0.lock() {
            let _ = f.write_line(&line);
        }
    }

    fn flush(&self) {}
}

/// Installs the process logger writing `<dir>/waystone.log`; returns its path.
pub fn init(dir: &Path) -> anyhow::Result<PathBuf> {
    std::fs::create_dir_all(dir)?;
    let path = dir.join(LOG_FILE_NAME);
    let file = RotatingFile::open(&path, LOG_MAX_BYTES)?;
    log::set_logger(Box::leak(Box::new(FileLogger(Mutex::new(file)))))
        .map_err(|e| anyhow::anyhow!("logger already installed: {e}"))?;
    log::set_max_level(log::LevelFilter::Info);
    Ok(path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rotates_past_max_and_discards_previous_old() {
        let dir = tempfile::tempdir().unwrap();
        let p = dir.path().join("waystone.log");
        std::fs::write(dir.path().join("waystone.log.old"), b"ancient").unwrap();
        std::fs::write(&p, vec![b'x'; 100]).unwrap();
        rotate_if_needed(&p, 50);
        assert!(!p.exists());
        assert_eq!(
            std::fs::read(dir.path().join("waystone.log.old"))
                .unwrap()
                .len(),
            100
        );
    }

    #[test]
    fn small_file_is_kept_and_writes_rotate_when_crossing() {
        let dir = tempfile::tempdir().unwrap();
        let p = dir.path().join("waystone.log");
        let mut f = RotatingFile::open(&p, 20).unwrap();
        f.write_line("0123456789").unwrap();
        f.write_line("0123456789").unwrap();
        f.write_line("next").unwrap();
        assert!(dir.path().join("waystone.log.old").exists());
        assert_eq!(std::fs::read_to_string(&p).unwrap(), "next\n");
    }
}
