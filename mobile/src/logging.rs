use crate::error::WaystoneError;
use std::sync::Arc;

#[uniffi::export(with_foreign)]
pub trait LogSink: Send + Sync {
    fn log(&self, level: String, message: String) -> Result<(), WaystoneError>;
}

struct Bridge(Arc<dyn LogSink>);

impl log::Log for Bridge {
    fn enabled(&self, m: &log::Metadata) -> bool {
        m.level() <= log::Level::Info
    }
    fn log(&self, r: &log::Record) {
        // A failing sink cannot report through itself (it would recurse); stderr is the only fallback.
        if self.enabled(r.metadata())
            && let Err(e) = self.0.log(r.level().to_string(), r.args().to_string())
        {
            eprintln!("[app] log sink failed: {e}");
        }
    }
    fn flush(&self) {}
}

#[uniffi::export]
pub fn install_log_sink(sink: Arc<dyn LogSink>) -> bool {
    match log::set_boxed_logger(Box::new(Bridge(sink))) {
        Ok(()) => {
            log::set_max_level(log::LevelFilter::Info);
            log::info!("[app] engine log sink installed");
            true
        }
        Err(_) => false,
    }
}
