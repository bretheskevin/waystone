use crate::config::{SyncTarget, WaystoneConfig};
use crate::net_status::{NoInternetReason, RETRY_SUFFIX};
use crate::presets::PRESETS;
use crate::sync_run::{RunMode, Selection};
use crate::updater::{CheckResult, Release, UpdateError};
use crate::webdav::{BlockingWebDav, WebDavClient};
use crossterm::event::KeyEvent;
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use waystone_core::crypto::Vault;
use waystone_sync::engine::{SyncPhase, SyncSummary};
use zeroize::Zeroize;

const MAX_LOG_LINES: usize = 200;
/// `App::busy` value while a multi-target sync runs (no target index matches it).
pub const SYNC_BUSY: usize = usize::MAX;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Screen {
    Dashboard,
    Conflicts,
    History,
    Snapshots,
    Setup,
    Settings,
}

pub enum Cmd {
    StartSync {
        selection: Selection,
        mode: RunMode,
    },
    RefreshStatus(usize),
    RefreshAllStatuses,
    AttemptUnlock {
        passphrase: String,
        webdav_password: Option<String>,
        recovery: bool,
    },
    SaveConfig,
    Quit,
    Resolve(ResolveJob),
    ScanConflicts,
    RebuildCreds {
        password: String,
    },
    ClearSession,
    CheckUpdate,
    InstallUpdate(Release),
    RunSetup {
        server_url: String,
        username: String,
        password: String,
        passphrase: String,
    },
    SaveRecoveryFile {
        key: String,
        device_id: String,
    },
    SaveSettings(WaystoneConfig),
    LoadHistory {
        target_id: usize,
    },
    RestoreHistory {
        target_id: usize,
        save_key: String,
        hash: String,
    },
    LoadSnapshots {
        target_id: usize,
    },
    RestoreSnapshot {
        target_id: usize,
        save_key: String,
        timestamp: String,
    },
}

impl std::fmt::Debug for Cmd {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::StartSync { selection, mode } => f
                .debug_struct("StartSync")
                .field("selection", selection)
                .field("mode", mode)
                .finish(),
            Self::RefreshStatus(id) => f.debug_tuple("RefreshStatus").field(id).finish(),
            Self::RefreshAllStatuses => write!(f, "RefreshAllStatuses"),
            Self::AttemptUnlock { recovery, .. } => f
                .debug_struct("AttemptUnlock")
                .field("passphrase", &"[REDACTED]")
                .field("webdav_password", &"[REDACTED]")
                .field("recovery", recovery)
                .finish(),
            Self::SaveConfig => write!(f, "SaveConfig"),
            Self::Quit => write!(f, "Quit"),
            Self::Resolve(job) => f.debug_tuple("Resolve").field(job).finish(),
            Self::ScanConflicts => write!(f, "ScanConflicts"),
            Self::RebuildCreds { .. } => f
                .debug_struct("RebuildCreds")
                .field("password", &"[REDACTED]")
                .finish(),
            Self::ClearSession => write!(f, "ClearSession"),
            Self::CheckUpdate => write!(f, "CheckUpdate"),
            Self::InstallUpdate(rel) => f.debug_tuple("InstallUpdate").field(&rel.version).finish(),
            Self::RunSetup { server_url, .. } => f
                .debug_struct("RunSetup")
                .field("server_url", server_url)
                .field("username", &"[REDACTED]")
                .field("password", &"[REDACTED]")
                .field("passphrase", &"[REDACTED]")
                .finish(),
            Self::SaveRecoveryFile { .. } => f
                .debug_struct("SaveRecoveryFile")
                .field("key", &"[REDACTED]")
                .finish(),
            Self::SaveSettings(cfg) => f
                .debug_struct("SaveSettings")
                .field("server_url", &cfg.server_url)
                .finish(),
            Self::LoadHistory { target_id } => f
                .debug_struct("LoadHistory")
                .field("target_id", target_id)
                .finish(),
            Self::RestoreHistory {
                target_id,
                save_key,
                ..
            } => f
                .debug_struct("RestoreHistory")
                .field("target_id", target_id)
                .field("save_key", save_key)
                .finish(),
            Self::LoadSnapshots { target_id } => f
                .debug_struct("LoadSnapshots")
                .field("target_id", target_id)
                .finish(),
            Self::RestoreSnapshot {
                target_id,
                save_key,
                ..
            } => f
                .debug_struct("RestoreSnapshot")
                .field("target_id", target_id)
                .field("save_key", save_key)
                .finish(),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResolveJob {
    KeepLocal {
        target_id: usize,
        group_key: String,
    },
    KeepRemote {
        target_id: usize,
        group_key: String,
        hash: String,
        head_mtime: String,
    },
}

impl ResolveJob {
    pub fn key(&self) -> (usize, &str) {
        match self {
            Self::KeepLocal {
                target_id,
                group_key,
            }
            | Self::KeepRemote {
                target_id,
                group_key,
                ..
            } => (*target_id, group_key),
        }
    }
}

#[derive(Debug)]
pub enum ActionResult {
    Ok(String),
    Err(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HeadInfo {
    pub hash: String,
    pub mtime: String,
    pub device_id: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ConflictEntry {
    pub target_id: usize,
    pub label: String,
    pub save_key: String,
    pub local: HeadInfo,
    pub remote: HeadInfo,
    pub remote_hash: String,
    /// Newest head mtime; becomes this device's base mtime after keep-remote.
    pub head_mtime: String,
    pub queued: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HistoryView {
    pub target_id: usize,
    pub save_key: String,
    pub label: String,
    pub timestamp: String,
    pub device_id: String,
    pub hash: String,
    pub mtime: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotView {
    pub target_id: usize,
    pub save_key: String,
    pub label: String,
    pub timestamp: String,
    pub file_count: usize,
    pub total_bytes: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SaveRow {
    pub target_id: usize,
    pub group_key: String,
    pub name: String,
    pub slot: String,
    pub status: TargetStatus,
    pub remote_only: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Pane {
    Targets,
    Saves,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SyncRunView {
    pub total: usize,
    pub index: Option<usize>,
    pub current: Option<String>,
    pub phase: Option<SyncPhase>,
    pub cancelling: bool,
    pub summary: Option<SyncSummary>,
}

impl SyncRunView {
    pub fn running(&self) -> bool {
        self.summary.is_none()
    }

    pub fn ratio(&self) -> f64 {
        if self.summary.is_some() {
            1.0
        } else if self.total == 0 {
            0.0
        } else {
            self.index
                .map_or(0.0, |i| (i + 1) as f64 / self.total as f64)
        }
    }
}

pub enum Msg {
    Key(KeyEvent),
    Tick,
    Progress {
        target_id: usize,
        phase: String,
    },
    ActionDone {
        target_id: usize,
        result: ActionResult,
    },
    Status {
        target_id: usize,
        status: TargetStatus,
    },
    /// `target` = the target the scan started for (see `current_target_id`).
    Rows {
        target_id: usize,
        target: SyncTarget,
        rows: Vec<SaveRow>,
        conflicts: Vec<ConflictEntry>,
    },
    SyncStarted {
        total: usize,
    },
    SyncItem {
        index: usize,
        total: usize,
        name: String,
        phase: SyncPhase,
    },
    SyncFinished(SyncSummary),
    ConflictFound(ConflictEntry),
    ConflictScanDone {
        error: Option<String>,
    },
    ResolveDone {
        target_id: usize,
        group_key: String,
        result: Result<(), String>,
    },
    UpdateChecked(Result<CheckResult, UpdateError>),
    UpdateProgress {
        done: u64,
        total: Option<u64>,
    },
    UpdateInstalled(Result<String, UpdateError>),
    Offline(Option<NoInternetReason>),
    CredsRebuilt {
        dav: Arc<WebDavClient>,
        blocking_dav: Arc<BlockingWebDav>,
    },
    History {
        target_id: usize,
        target: SyncTarget,
        entries: Vec<HistoryView>,
    },
    Snapshots {
        target_id: usize,
        target: SyncTarget,
        entries: Vec<SnapshotView>,
    },
    UnlockOk {
        vault: Arc<Vault>,
        dav: Arc<WebDavClient>,
        blocking_dav: Arc<BlockingWebDav>,
    },
    UnlockFailed(String),
    SetupResult(Result<SetupOk, String>),
    RecoverySaved(Result<std::path::PathBuf, String>),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum TargetStatus {
    Unknown,
    Checking,
    InSync,
    Ahead,
    Behind,
    Conflict,
    Error(String),
}

impl TargetStatus {
    pub fn label(&self) -> &str {
        match self {
            Self::Unknown => "unknown",
            Self::Checking => "checking",
            Self::InSync => "in_sync",
            Self::Ahead => "ahead",
            Self::Behind => "behind",
            Self::Conflict => "conflict",
            Self::Error(_) => "error",
        }
    }
}

pub fn worst_status(a: TargetStatus, b: TargetStatus) -> TargetStatus {
    fn rank(s: &TargetStatus) -> u8 {
        match s {
            TargetStatus::Ahead => 1,
            TargetStatus::Behind => 2,
            TargetStatus::Conflict => 3,
            TargetStatus::Error(_) => 4,
            _ => 0,
        }
    }
    if rank(&b) > rank(&a) { b } else { a }
}

pub struct SessionCreds {
    pub vault: Arc<Vault>,
    pub dav: Arc<WebDavClient>,
    pub blocking_dav: Arc<BlockingWebDav>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FormMode {
    Add,
    Edit(usize),
}

#[derive(Clone)]
pub enum Overlay {
    None,
    Unlock {
        /// Holds passphrase in normal mode, or the recovery key when recovery_mode is true.
        passphrase: String,
        webdav_password: String,
        needs_webdav: bool,
        field_index: usize,
        error: Option<String>,
        then: Option<PendingCmd>,
        recovery_mode: bool,
    },
    TargetForm {
        mode: FormMode,
        name: String,
        path: String,
        adapter: String,
        system: String,
        /// 0 = Manual, n = PRESETS[n - 1]
        preset: usize,
        save_dir: String,
        field_index: usize,
        error: Option<String>,
    },
    Confirm {
        message: String,
        action: PendingCmd,
    },
    Help,
    RecoveryKey {
        key: String,
    },
}

impl std::fmt::Debug for Overlay {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::None => write!(f, "None"),
            Self::Unlock {
                needs_webdav,
                field_index,
                error,
                then,
                recovery_mode,
                ..
            } => f
                .debug_struct("Unlock")
                .field("passphrase", &"[REDACTED]")
                .field("webdav_password", &"[REDACTED]")
                .field("needs_webdav", needs_webdav)
                .field("field_index", field_index)
                .field("error", error)
                .field("then", then)
                .field("recovery_mode", recovery_mode)
                .finish(),
            Self::TargetForm {
                mode,
                name,
                field_index,
                error,
                ..
            } => f
                .debug_struct("TargetForm")
                .field("mode", mode)
                .field("name", name)
                .field("field_index", field_index)
                .field("error", error)
                .finish(),
            Self::Confirm { message, action } => f
                .debug_struct("Confirm")
                .field("message", message)
                .field("action", action)
                .finish(),
            Self::Help => write!(f, "Help"),
            Self::RecoveryKey { .. } => f
                .debug_struct("RecoveryKey")
                .field("key", &"[REDACTED]")
                .finish(),
        }
    }
}

#[derive(Default)]
pub struct SetupForm {
    pub server_url: String,
    pub username: String,
    pub password: String,
    pub passphrase: String,
    pub confirm: String,
    pub field: usize,
    pub error: Option<String>,
    pub busy: bool,
}

impl std::fmt::Debug for SetupForm {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SetupForm")
            .field("server_url", &self.server_url)
            .field("username", &self.username)
            .field("password", &"[REDACTED]")
            .field("passphrase", &"[REDACTED]")
            .field("confirm", &"[REDACTED]")
            .field("field", &self.field)
            .field("error", &self.error)
            .field("busy", &self.busy)
            .finish()
    }
}

impl Drop for SetupForm {
    fn drop(&mut self) {
        self.password.zeroize();
        self.passphrase.zeroize();
        self.confirm.zeroize();
    }
}

#[derive(Clone)]
pub struct SettingsForm {
    pub server_url: String,
    pub conflict_policy: waystone_core::conflict::ConflictPolicy,
    pub username: String,
    /// Empty = unchanged.
    pub webdav_password: String,
    pub safety_backup: bool,
    pub field: usize,
    pub error: Option<String>,
}

pub const SETTINGS_FIELD_PASSWORD: usize = 4;
pub const SETTINGS_FIELD_UPDATE: usize = 5;
pub const SETTINGS_FIELD_LOGOUT: usize = 6;

impl std::fmt::Debug for SettingsForm {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SettingsForm")
            .field("server_url", &self.server_url)
            .field("conflict_policy", &self.conflict_policy)
            .field("username", &self.username)
            .field("webdav_password", &"[REDACTED]")
            .field("safety_backup", &self.safety_backup)
            .field("field", &self.field)
            .field("error", &self.error)
            .finish()
    }
}

impl Drop for SettingsForm {
    fn drop(&mut self) {
        self.webdav_password.zeroize();
    }
}

pub struct SetupOk {
    pub recovery_key: String,
    pub vault: Arc<Vault>,
    pub dav: Arc<WebDavClient>,
    pub blocking_dav: Arc<BlockingWebDav>,
}

#[derive(Debug, Clone)]
pub enum PendingCmd {
    Sync {
        selection: Selection,
        mode: RunMode,
    },
    RefreshStatus(usize),
    #[allow(dead_code)]
    RefreshAllStatuses,
    DeleteTarget(usize),
    ResolveKeepLocal {
        target_id: usize,
        save_key: String,
    },
    ResolveKeepRemote {
        target_id: usize,
        save_key: String,
        remote_hash: String,
        head_mtime: String,
    },
    ScanConflicts,
    Logout,
    InstallUpdate(Release),
    LoadHistory {
        target_id: usize,
    },
}

#[derive(Debug, Clone)]
pub struct LogLine {
    pub text: String,
}

pub struct App {
    pub targets: Vec<SyncTarget>,
    pub selected: usize,
    pub statuses: HashMap<usize, TargetStatus>,
    pub log: VecDeque<LogLine>,
    pub overlay: Overlay,
    pub creds: Option<SessionCreds>,
    pub busy: Option<usize>,
    pub spinner: u8,
    pub dirty: bool,
    pub should_quit: bool,
    pub server_url: String,
    pub username: Option<String>,
    pub screen: Screen,
    pub conflicts: Vec<ConflictEntry>,
    pub conflict_sel: usize,
    pub device_id: String,
    pub conflict_policy: waystone_core::conflict::ConflictPolicy,
    pub safety_backup: bool,
    pub setup: Option<SetupForm>,
    pub settings: Option<SettingsForm>,
    pub history_entries: Vec<HistoryView>,
    pub history_selected: usize,
    pub snapshot_entries: Vec<SnapshotView>,
    pub snapshot_selected: usize,
    pub rows: Vec<SaveRow>,
    pub row_sel: usize,
    pub pane: Pane,
    pub sync_run: Option<SyncRunView>,
    pub sync_cancel: Arc<AtomicBool>,
    pub resolve_queue: VecDeque<ResolveJob>,
    pub resolving: bool,
    pub conflict_scanning: bool,
    pub conflict_notice: Option<String>,
    pub offline: Option<NoInternetReason>,
    pub retry_pending: bool,
    pub retry_failed: bool,
    pub update_status: Option<String>,
}

impl App {
    pub fn new(config: &WaystoneConfig) -> Self {
        let first_run = config.server_url.is_empty();
        Self {
            targets: config.targets.clone(),
            selected: 0,
            statuses: HashMap::new(),
            log: VecDeque::new(),
            overlay: Overlay::None,
            creds: Option::None,
            busy: Option::None,
            spinner: 0,
            dirty: false,
            should_quit: false,
            server_url: config.server_url.clone(),
            username: config.username.clone(),
            screen: if first_run {
                Screen::Setup
            } else {
                Screen::Dashboard
            },
            conflicts: Vec::new(),
            conflict_sel: 0,
            device_id: config.device_id.clone(),
            conflict_policy: config.conflict_policy,
            safety_backup: config.safety_backup,
            setup: if first_run {
                Some(SetupForm::default())
            } else {
                None
            },
            settings: None,
            history_entries: Vec::new(),
            history_selected: 0,
            snapshot_entries: Vec::new(),
            snapshot_selected: 0,
            rows: Vec::new(),
            row_sel: 0,
            pane: Pane::Targets,
            sync_run: None,
            sync_cancel: Arc::new(AtomicBool::new(false)),
            resolve_queue: VecDeque::new(),
            resolving: false,
            conflict_scanning: false,
            conflict_notice: None,
            offline: None,
            retry_pending: false,
            retry_failed: false,
            update_status: None,
        }
    }

    /// Red banner text while offline (`r` retries).
    pub fn offline_banner(&self) -> Option<String> {
        let reason = self.offline?;
        let suffix = if self.retry_failed {
            format!(" {RETRY_SUFFIX}")
        } else {
            String::new()
        };
        Some(format!(
            "{} \u{2014} {} (r to retry){suffix}",
            reason.headline(),
            reason.detail()
        ))
    }

    /// A sync was requested but its run (progress overlay) has not started yet (e.g. probing).
    pub fn sync_starting(&self) -> bool {
        self.busy == Some(SYNC_BUSY) && self.sync_run.is_none()
    }

    pub fn rows_for(&self, target_id: usize) -> Vec<&SaveRow> {
        self.rows
            .iter()
            .filter(|r| r.target_id == target_id)
            .collect()
    }

    pub fn push_log(&mut self, text: String) {
        log::info!("[ui] {text}");
        self.log.push_back(LogLine { text });
        while self.log.len() > MAX_LOG_LINES {
            self.log.pop_front();
        }
    }

    pub fn needs_webdav_password(&self) -> bool {
        crate::cli_session::env_webdav_password().is_none()
    }

    pub fn to_config(&self, _base: &WaystoneConfig) -> WaystoneConfig {
        WaystoneConfig {
            device_id: self.device_id.clone(),
            server_url: self.server_url.clone(),
            conflict_policy: self.conflict_policy,
            username: self.username.clone(),
            targets: self.targets.clone(),
            safety_backup: self.safety_backup,
        }
    }
}

#[allow(dead_code)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SetupDecision {
    Init,
    RedirectUnlock,
}

#[allow(dead_code)]
pub fn setup_decision(keys_json_present: bool) -> SetupDecision {
    if keys_json_present {
        SetupDecision::RedirectUnlock
    } else {
        SetupDecision::Init
    }
}

pub fn update(app: &mut App, msg: Msg) -> Vec<Cmd> {
    let cmds = update_inner(app, msg);
    if cmds.iter().any(|c| matches!(c, Cmd::StartSync { .. })) {
        app.busy = Some(SYNC_BUSY);
    }
    cmds
}

fn set_conflicts(app: &mut App, target_id: usize, mut entries: Vec<ConflictEntry>) {
    // A refresh landing mid-resolve must not un-queue a pending job (it could be resolved twice).
    for e in &mut entries {
        e.queued = app
            .conflicts
            .iter()
            .any(|c| c.target_id == target_id && c.save_key == e.save_key && c.queued);
    }
    app.conflicts.retain(|c| c.target_id != target_id);
    app.conflicts.extend(entries);
    if app.conflict_sel >= app.conflicts.len() && !app.conflicts.is_empty() {
        app.conflict_sel = app.conflicts.len() - 1;
    }
    if app.conflicts.is_empty() {
        app.conflict_sel = 0;
    }
}

/// Where an in-flight result's target sits now: a reorder/delete/edit while a scan or history
/// load ran must never route its rows (or a later restore) to whichever target took that index.
fn current_target_id(app: &mut App, target_id: usize, target: &SyncTarget) -> Option<usize> {
    if app.targets.get(target_id) == Some(target) {
        return Some(target_id);
    }
    let now = app.targets.iter().position(|t| t == target);
    log::info!(
        "[ui] result for target '{}' (was #{target_id}) remapped to {now:?}",
        target.name
    );
    if now.is_none() {
        app.push_log(format!(
            "Dropped a stale result for '{}' (target changed).",
            target.name
        ));
    }
    now
}

/// Setup + Settings server URL rule.
fn validate_server_url(url: &str) -> Result<(), &'static str> {
    if url.trim().is_empty() {
        Err("Server URL is required.")
    } else if !url.starts_with("http://") && !url.starts_with("https://") {
        Err("Server URL must start with http:// or https://")
    } else {
        Ok(())
    }
}

fn clamp_row_sel(app: &mut App) {
    let n = app.rows_for(app.selected).len();
    app.row_sel = app.row_sel.min(n.saturating_sub(1));
}

fn update_inner(app: &mut App, msg: Msg) -> Vec<Cmd> {
    match msg {
        Msg::Tick => {
            app.spinner = app.spinner.wrapping_add(1);
            vec![]
        }

        Msg::Key(key) => handle_key(app, key),

        Msg::Progress { target_id, phase } => {
            app.push_log(format!("[{}] {}", target_id, phase));
            vec![]
        }

        Msg::ActionDone { target_id, result } => {
            app.busy = Option::None;
            match result {
                ActionResult::Ok(msg) => {
                    app.push_log(format!("[{}] Done: {}", target_id, msg));
                }
                ActionResult::Err(err) => {
                    app.statuses
                        .insert(target_id, TargetStatus::Error(err.clone()));
                    app.push_log(format!("[{}] Error: {}", target_id, err));
                }
            }
            vec![]
        }

        Msg::Status { target_id, status } => {
            app.statuses.insert(target_id, status);
            vec![]
        }

        Msg::Rows {
            target_id,
            target,
            mut rows,
            mut conflicts,
        } => {
            let Some(target_id) = current_target_id(app, target_id, &target) else {
                return vec![];
            };
            rows.iter_mut().for_each(|r| r.target_id = target_id);
            conflicts.iter_mut().for_each(|c| c.target_id = target_id);
            let status = rows.iter().fold(TargetStatus::InSync, |acc, r| {
                worst_status(acc, r.status.clone())
            });
            app.rows.retain(|r| r.target_id != target_id);
            app.rows.extend(rows);
            app.statuses.insert(target_id, status);
            set_conflicts(app, target_id, conflicts);
            clamp_row_sel(app);
            vec![]
        }

        // The flag is reset when the sync is requested (Cmd::StartSync), so a cancel pressed
        // while planning still stops execute before its first item.
        Msg::SyncStarted { total } => {
            app.sync_run = Some(SyncRunView {
                total,
                ..Default::default()
            });
            vec![]
        }

        Msg::SyncItem {
            index,
            total,
            name,
            phase,
        } => {
            if let Some(run) = app.sync_run.as_mut() {
                run.total = total;
                run.index = Some(index);
                run.current = Some(name);
                run.phase = Some(phase);
            }
            vec![]
        }

        Msg::SyncFinished(summary) => {
            app.busy = Option::None;
            app.push_log(summary.headline());
            for m in &summary.error_messages {
                app.push_log(format!("  {m}"));
            }
            let run = app.sync_run.get_or_insert_with(SyncRunView::default);
            run.summary = Some(summary);
            vec![]
        }

        Msg::ConflictFound(mut entry) => {
            entry.queued = app
                .resolve_queue
                .iter()
                .any(|j| j.key() == (entry.target_id, entry.save_key.as_str()));
            match app
                .conflicts
                .iter_mut()
                .find(|c| c.target_id == entry.target_id && c.save_key == entry.save_key)
            {
                Some(existing) => *existing = entry,
                None => app.conflicts.push(entry),
            }
            vec![]
        }

        Msg::ConflictScanDone { error } => {
            app.conflict_scanning = false;
            let mut cmds = Vec::new();
            match error {
                Some(e) => {
                    app.push_log(format!("Conflict scan failed: {e}"));
                    app.conflict_notice = Some(e);
                }
                None => finish_inbox_if_empty(app, &mut cmds),
            }
            cmds
        }

        Msg::ResolveDone {
            target_id,
            group_key,
            result,
        } => {
            app.resolving = false;
            match result {
                Ok(()) => {
                    app.conflicts
                        .retain(|c| !(c.target_id == target_id && c.save_key == group_key));
                    app.conflict_sel = app.conflict_sel.min(app.conflicts.len().saturating_sub(1));
                    app.push_log(format!("Resolved {group_key}."));
                }
                Err(e) => {
                    if let Some(c) = app
                        .conflicts
                        .iter_mut()
                        .find(|c| c.target_id == target_id && c.save_key == group_key)
                    {
                        c.queued = false;
                    }
                    app.push_log(format!("Resolve of {group_key} failed: {e}"));
                    app.conflict_notice = Some(e);
                }
            }
            let mut cmds = start_next_resolve(app);
            finish_inbox_if_empty(app, &mut cmds);
            cmds
        }

        Msg::UpdateChecked(result) => {
            match result {
                Ok(CheckResult::UpToDate(v)) => {
                    app.update_status = Some(format!("Up to date (v{v})"));
                }
                Ok(CheckResult::Available(rel)) => {
                    app.update_status = Some(format!("Update available: v{}", rel.version));
                    app.overlay = Overlay::Confirm {
                        message: format!("Install v{}?", rel.version),
                        action: PendingCmd::InstallUpdate(rel),
                    };
                }
                Err(e) => app.update_status = Some(e.check_message()),
            }
            vec![]
        }

        Msg::UpdateProgress { done, total } => {
            app.update_status = Some(match total.filter(|t| *t > 0) {
                Some(t) => format!("Downloading... {}%", done * 100 / t),
                None => format!("Downloading... {} KiB", done / 1024),
            });
            vec![]
        }

        Msg::UpdateInstalled(result) => {
            app.update_status = Some(match result {
                Ok(v) => format!("Updated to v{v} \u{2014} restart waystone to use it"),
                Err(e) => e.install_message().to_string(),
            });
            vec![]
        }

        Msg::Offline(reason) => {
            match reason {
                None => {
                    app.offline = None;
                    app.retry_pending = false;
                    app.retry_failed = false;
                }
                Some(r) => {
                    app.retry_failed = app.retry_pending;
                    app.retry_pending = false;
                    app.offline = Some(r);
                    if app.sync_starting() {
                        app.busy = None;
                    }
                    if let Some(f) = app.setup.as_mut() {
                        f.busy = false;
                        f.error = Some(r.headline().into());
                    }
                    if let Overlay::Unlock { error, .. } = &mut app.overlay {
                        *error = Some(r.headline().into());
                    }
                }
            }
            vec![]
        }

        Msg::CredsRebuilt { dav, blocking_dav } => {
            if let Some(c) = app.creds.as_mut() {
                c.dav = dav;
                c.blocking_dav = blocking_dav;
                app.push_log("WebDAV password updated.".into());
            }
            vec![]
        }

        Msg::History {
            target_id,
            target,
            mut entries,
        } => {
            let Some(target_id) = current_target_id(app, target_id, &target) else {
                return vec![];
            };
            entries.iter_mut().for_each(|e| e.target_id = target_id);
            app.history_entries = entries;
            app.history_selected = 0;
            app.screen = Screen::History;
            vec![]
        }

        Msg::Snapshots {
            target_id,
            target,
            mut entries,
        } => {
            let Some(target_id) = current_target_id(app, target_id, &target) else {
                return vec![];
            };
            entries.iter_mut().for_each(|e| e.target_id = target_id);
            app.snapshot_entries = entries;
            app.snapshot_selected = 0;
            app.screen = Screen::Snapshots;
            vec![]
        }

        Msg::UnlockOk {
            vault,
            dav,
            blocking_dav,
        } => {
            let pending = match &app.overlay {
                Overlay::Unlock { then, .. } => then.clone(),
                _ => Option::None,
            };
            app.creds = Some(SessionCreds {
                vault,
                dav,
                blocking_dav,
            });
            app.overlay = Overlay::None;
            app.push_log("Session unlocked.".into());
            match pending {
                Some(p) => run_pending(app, p),
                None => vec![Cmd::RefreshAllStatuses],
            }
        }

        Msg::UnlockFailed(err) => {
            if let Overlay::Unlock {
                ref mut error,
                ref mut passphrase,
                ref mut webdav_password,
                ..
            } = app.overlay
            {
                passphrase.zeroize();
                webdav_password.zeroize();
                *error = Some(err);
            }
            vec![]
        }

        Msg::SetupResult(result) => match result {
            Ok(ok) => {
                if let Some(ref form) = app.setup {
                    app.server_url = form.server_url.clone();
                    app.username = if form.username.is_empty() {
                        None
                    } else {
                        Some(form.username.clone())
                    };
                }
                app.creds = Some(SessionCreds {
                    vault: ok.vault,
                    dav: ok.dav,
                    blocking_dav: ok.blocking_dav,
                });
                app.setup = None;
                app.screen = Screen::Dashboard;
                app.overlay = Overlay::RecoveryKey {
                    key: ok.recovery_key,
                };
                app.push_log("Vault initialized successfully.".into());
                vec![Cmd::SaveConfig, Cmd::RefreshAllStatuses]
            }
            Err(err) => {
                if let Some(ref mut form) = app.setup {
                    form.busy = false;
                    form.error = Some(err);
                }
                vec![]
            }
        },

        Msg::RecoverySaved(result) => {
            match result {
                Ok(path) => {
                    app.push_log(format!("Recovery key saved to {}", path.display()));
                }
                Err(err) => {
                    app.push_log(format!("Failed to save recovery key: {}", err));
                }
            }
            vec![]
        }
    }
}

fn enqueue_resolve(app: &mut App, job: ResolveJob) -> Vec<Cmd> {
    for c in &mut app.conflicts {
        if (c.target_id, c.save_key.as_str()) == job.key() {
            c.queued = true;
        }
    }
    app.conflict_notice = None;
    app.resolve_queue.push_back(job);
    start_next_resolve(app)
}

fn start_next_resolve(app: &mut App) -> Vec<Cmd> {
    if app.resolving {
        return vec![];
    }
    match app.resolve_queue.pop_front() {
        Some(job) => {
            app.resolving = true;
            vec![Cmd::Resolve(job)]
        }
        None => vec![],
    }
}

fn finish_inbox_if_empty(app: &mut App, cmds: &mut Vec<Cmd>) {
    if app.screen == Screen::Conflicts
        && app.conflicts.is_empty()
        && app.resolve_queue.is_empty()
        && !app.resolving
        && !app.conflict_scanning
    {
        app.screen = Screen::Dashboard;
        app.conflict_sel = 0;
        cmds.push(Cmd::RefreshAllStatuses);
    }
}

fn begin_conflict_scan(app: &mut App) -> Vec<Cmd> {
    app.conflict_scanning = true;
    app.conflict_notice = None;
    app.conflicts.clear();
    app.conflict_sel = 0;
    vec![Cmd::ScanConflicts]
}

/// Runs a pending command once credentials exist; queue-based ones are enqueued, not dispatched.
fn run_pending(app: &mut App, p: PendingCmd) -> Vec<Cmd> {
    match p {
        PendingCmd::Sync { selection, mode } => vec![Cmd::StartSync { selection, mode }],
        PendingCmd::RefreshStatus(id) => vec![Cmd::RefreshStatus(id)],
        PendingCmd::RefreshAllStatuses => vec![Cmd::RefreshAllStatuses],
        PendingCmd::ScanConflicts => begin_conflict_scan(app),
        PendingCmd::ResolveKeepLocal {
            target_id,
            save_key,
        } => enqueue_resolve(
            app,
            ResolveJob::KeepLocal {
                target_id,
                group_key: save_key,
            },
        ),
        PendingCmd::ResolveKeepRemote {
            target_id,
            save_key,
            remote_hash,
            head_mtime,
        } => enqueue_resolve(
            app,
            ResolveJob::KeepRemote {
                target_id,
                group_key: save_key,
                hash: remote_hash,
                head_mtime,
            },
        ),
        PendingCmd::InstallUpdate(rel) => vec![Cmd::InstallUpdate(rel)],
        PendingCmd::LoadHistory { target_id } => vec![Cmd::LoadHistory { target_id }],
        // handled directly by the confirm overlay; never queued behind an unlock
        PendingCmd::DeleteTarget(_) | PendingCmd::Logout => vec![],
    }
}

fn handle_sync_run_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Some(running) = app.sync_run.as_ref().map(SyncRunView::running) else {
        return vec![];
    };
    match key.code {
        KeyCode::Char('q') => vec![Cmd::Quit],
        KeyCode::Char('c') | KeyCode::Esc if running => {
            if let Some(run) = app.sync_run.as_mut() {
                run.cancelling = true;
            }
            app.sync_cancel.store(true, Ordering::SeqCst);
            app.push_log("Cancelling after the current save...".into());
            vec![]
        }
        KeyCode::Enter | KeyCode::Esc if !running => {
            app.sync_run = None;
            vec![Cmd::RefreshAllStatuses]
        }
        _ => vec![],
    }
}

fn handle_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    if app.sync_run.is_some() && matches!(app.overlay, Overlay::None) {
        return handle_sync_run_key(app, key);
    }
    match &app.overlay {
        Overlay::None => match app.screen {
            Screen::Dashboard => handle_dashboard_key(app, key),
            Screen::Conflicts => handle_conflicts_key(app, key),
            Screen::History => handle_history_key(app, key),
            Screen::Snapshots => handle_snapshots_key(app, key),
            Screen::Setup => handle_setup_key(app, key),
            Screen::Settings => handle_settings_key(app, key),
        },
        Overlay::Unlock { .. } => handle_unlock_key(app, key),
        Overlay::TargetForm { .. } => handle_form_key(app, key),
        Overlay::Confirm { .. } => handle_confirm_key(app, key),
        Overlay::Help => {
            app.overlay = Overlay::None;
            vec![]
        }
        Overlay::RecoveryKey { .. } => handle_recovery_key_overlay(app, key),
    }
}

/// While busy only quit works, plus cancel while a sync is still planning (before its overlay).
fn busy_keys(app: &mut App, key: KeyEvent) -> Option<Vec<Cmd>> {
    use crossterm::event::KeyCode;

    app.busy?;
    if key.code == KeyCode::Char('q') {
        return Some(vec![Cmd::Quit]);
    }
    if app.sync_starting() && matches!(key.code, KeyCode::Char('c') | KeyCode::Esc) {
        app.sync_cancel.store(true, Ordering::SeqCst);
        app.push_log("Cancelling: nothing will be written once planning ends.".into());
    }
    Some(vec![])
}

fn handle_dashboard_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if let Some(cmds) = busy_keys(app, key) {
        return cmds;
    }

    match key.code {
        KeyCode::Char('q') => vec![Cmd::Quit],
        KeyCode::Char('?') => {
            app.overlay = Overlay::Help;
            vec![]
        }
        KeyCode::Tab => {
            app.pane = match app.pane {
                Pane::Targets => Pane::Saves,
                Pane::Saves => Pane::Targets,
            };
            vec![]
        }
        KeyCode::Char('A') => {
            if app.targets.is_empty() {
                return vec![];
            }
            ensure_creds_then(
                app,
                PendingCmd::Sync {
                    selection: Selection::All,
                    mode: RunMode::Sync,
                },
            )
        }
        KeyCode::Char('r') => {
            if app.targets.is_empty() {
                return vec![];
            }
            app.retry_pending = app.offline.is_some();
            ensure_creds_then(app, PendingCmd::RefreshStatus(app.selected))
        }
        KeyCode::Char('C') => {
            app.screen = Screen::Conflicts;
            app.conflict_sel = 0;
            if app.resolving || !app.resolve_queue.is_empty() {
                // A rescan would re-list the in-flight save as actionable; keep the current inbox.
                return vec![];
            }
            ensure_creds_then(app, PendingCmd::ScanConflicts)
        }
        KeyCode::Char('S') => {
            app.settings = Some(SettingsForm {
                server_url: app.server_url.clone(),
                conflict_policy: app.conflict_policy,
                username: app.username.clone().unwrap_or_default(),
                webdav_password: String::new(),
                safety_backup: app.safety_backup,
                field: 0,
                error: None,
            });
            app.update_status = None;
            app.screen = Screen::Settings;
            vec![]
        }
        KeyCode::Char('h') => {
            if app.targets.is_empty() {
                return vec![];
            }
            ensure_creds_then(
                app,
                PendingCmd::LoadHistory {
                    target_id: app.selected,
                },
            )
        }
        KeyCode::Char('b') => {
            if app.targets.is_empty() {
                return vec![];
            }
            vec![Cmd::LoadSnapshots {
                target_id: app.selected,
            }]
        }
        _ => match app.pane {
            Pane::Targets => handle_targets_pane_key(app, key),
            Pane::Saves => handle_saves_pane_key(app, key),
        },
    }
}

/// Re-keys per-target UI state after the target list changed (`map`: old index -> new, None = removed).
fn reindex_target_state(app: &mut App, map: impl Fn(usize) -> Option<usize>) {
    app.rows
        .retain_mut(|r| map(r.target_id).map(|n| r.target_id = n).is_some());
    app.conflicts
        .retain_mut(|c| map(c.target_id).map(|n| c.target_id = n).is_some());
    app.statuses = std::mem::take(&mut app.statuses)
        .into_iter()
        .filter_map(|(id, s)| Some((map(id)?, s)))
        .collect();
    app.conflict_sel = app.conflict_sel.min(app.conflicts.len().saturating_sub(1));
}

fn move_selected_target(app: &mut App, to: usize) {
    let from = app.selected;
    app.targets.swap(from, to);
    reindex_target_state(app, |i| {
        Some(if i == from {
            to
        } else if i == to {
            from
        } else {
            i
        })
    });
    app.selected = to;
    app.dirty = true;
}

fn sync_selected_target(app: &mut App, mode: RunMode) -> Vec<Cmd> {
    if app.targets.is_empty() {
        return vec![];
    }
    ensure_creds_then(
        app,
        PendingCmd::Sync {
            selection: Selection::Target(app.selected),
            mode,
        },
    )
}

/// Conflict scans and queued resolves carry target indices: reordering, editing or deleting targets
/// under them would aim a resolve at the wrong target.
fn target_edits_blocked(app: &mut App) -> bool {
    let blocked = app.conflict_scanning || app.resolving || !app.resolve_queue.is_empty();
    if blocked {
        app.push_log(
            "Wait for the conflict scan and resolves to finish before changing targets.".into(),
        );
    }
    blocked
}

fn handle_targets_pane_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if matches!(key.code, KeyCode::Char('J' | 'K' | 'e' | 'd')) && target_edits_blocked(app) {
        return vec![];
    }
    match key.code {
        KeyCode::Down | KeyCode::Char('j') => {
            if !app.targets.is_empty() {
                app.selected = (app.selected + 1).min(app.targets.len() - 1);
                app.row_sel = 0;
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.selected > 0 {
                app.selected -= 1;
                app.row_sel = 0;
            }
            vec![]
        }
        KeyCode::Char('J') => {
            if app.selected + 1 < app.targets.len() {
                move_selected_target(app, app.selected + 1);
            }
            vec![]
        }
        KeyCode::Char('K') => {
            if app.selected > 0 {
                move_selected_target(app, app.selected - 1);
            }
            vec![]
        }
        KeyCode::Char('a') => {
            app.overlay = Overlay::TargetForm {
                mode: FormMode::Add,
                name: String::new(),
                path: String::new(),
                adapter: "jksv".into(),
                system: "switch".into(),
                preset: 0,
                save_dir: String::new(),
                field_index: 0,
                error: Option::None,
            };
            vec![]
        }
        KeyCode::Char('e') => {
            if let Some(t) = app.targets.get(app.selected) {
                app.overlay = Overlay::TargetForm {
                    mode: FormMode::Edit(app.selected),
                    name: t.name.clone(),
                    path: t.path.to_string_lossy().into_owned(),
                    adapter: t.adapter.clone(),
                    system: t.system.clone(),
                    preset: preset_index(t.preset.as_deref()),
                    save_dir: t
                        .save_dir
                        .as_ref()
                        .map(|d| d.to_string_lossy().into_owned())
                        .unwrap_or_default(),
                    field_index: 0,
                    error: Option::None,
                };
            }
            vec![]
        }
        KeyCode::Char('d') => {
            if let Some(t) = app.targets.get(app.selected) {
                app.overlay = Overlay::Confirm {
                    message: format!("Delete target '{}'?", t.name),
                    action: PendingCmd::DeleteTarget(app.selected),
                };
            }
            vec![]
        }
        KeyCode::Char('s') => sync_selected_target(app, RunMode::Sync),
        KeyCode::Char('p') => sync_selected_target(app, RunMode::Push),
        KeyCode::Char('P') => sync_selected_target(app, RunMode::Pull),
        _ => vec![],
    }
}

fn handle_saves_pane_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let count = app.rows_for(app.selected).len();
    match key.code {
        KeyCode::Down | KeyCode::Char('j') => {
            if count > 0 {
                app.row_sel = (app.row_sel + 1).min(count - 1);
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            app.row_sel = app.row_sel.saturating_sub(1);
            vec![]
        }
        KeyCode::Char(c @ ('s' | 'p' | 'P')) => {
            let Some(row) = app
                .rows_for(app.selected)
                .get(app.row_sel)
                .copied()
                .cloned()
            else {
                return vec![];
            };
            let mode = match c {
                's' => RunMode::Sync,
                'p' => RunMode::Push,
                _ => RunMode::Pull,
            };
            if mode == RunMode::Push && row.remote_only {
                app.push_log("Nothing local to upload \u{2014} restore it with P".into());
                return vec![];
            }
            ensure_creds_then(
                app,
                PendingCmd::Sync {
                    selection: Selection::Save {
                        target: row.target_id,
                        group_key: row.group_key,
                    },
                    mode,
                },
            )
        }
        _ => vec![],
    }
}

pub fn preset_index(id: Option<&str>) -> usize {
    id.and_then(|id| PRESETS.iter().position(|p| p.id == id))
        .map_or(0, |i| i + 1)
}

/// Inverse of [`preset_index`]: 0 (Manual) and out-of-range indices are None.
fn preset_at(index: usize) -> Option<&'static crate::presets::EmulatorPreset> {
    PRESETS.get(index.checked_sub(1)?)
}

pub fn preset_label(index: usize) -> &'static str {
    preset_at(index).map_or("Manual", |p| p.label)
}

fn handle_conflicts_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if let Some(cmds) = busy_keys(app, key) {
        return cmds;
    }

    match key.code {
        KeyCode::Esc | KeyCode::Char('q') => {
            app.screen = Screen::Dashboard;
            vec![]
        }
        KeyCode::Down | KeyCode::Char('j') => {
            if !app.conflicts.is_empty() {
                app.conflict_sel = (app.conflict_sel + 1).min(app.conflicts.len() - 1);
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.conflict_sel > 0 {
                app.conflict_sel -= 1;
            }
            vec![]
        }
        KeyCode::Char('l') => match app.conflicts.get(app.conflict_sel) {
            Some(entry) if !entry.queued => {
                let pending = PendingCmd::ResolveKeepLocal {
                    target_id: entry.target_id,
                    save_key: entry.save_key.clone(),
                };
                ensure_creds_then(app, pending)
            }
            _ => vec![],
        },
        KeyCode::Char('r') => {
            if let Some(entry) = app.conflicts.get(app.conflict_sel).filter(|e| !e.queued) {
                app.overlay = Overlay::Confirm {
                    message: format!(
                        "Replace your local save of {} with the server copy from {}? A safety backup is taken first.",
                        entry.label,
                        entry
                            .remote
                            .device_id
                            .as_deref()
                            .unwrap_or("another device")
                    ),
                    action: PendingCmd::ResolveKeepRemote {
                        target_id: entry.target_id,
                        save_key: entry.save_key.clone(),
                        remote_hash: entry.remote_hash.clone(),
                        head_mtime: entry.head_mtime.clone(),
                    },
                };
            }
            vec![]
        }
        _ => vec![],
    }
}

fn handle_history_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if let Some(cmds) = busy_keys(app, key) {
        return cmds;
    }

    match key.code {
        KeyCode::Esc | KeyCode::Char('q') => {
            app.screen = Screen::Dashboard;
            vec![]
        }
        KeyCode::Down | KeyCode::Char('j') => {
            if !app.history_entries.is_empty() {
                app.history_selected =
                    (app.history_selected + 1).min(app.history_entries.len() - 1);
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.history_selected > 0 {
                app.history_selected -= 1;
            }
            vec![]
        }
        KeyCode::Enter => {
            if let Some(entry) = app.history_entries.get(app.history_selected) {
                vec![Cmd::RestoreHistory {
                    target_id: entry.target_id,
                    save_key: entry.save_key.clone(),
                    hash: entry.hash.clone(),
                }]
            } else {
                vec![]
            }
        }
        _ => vec![],
    }
}

fn handle_snapshots_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if let Some(cmds) = busy_keys(app, key) {
        return cmds;
    }

    match key.code {
        KeyCode::Esc | KeyCode::Char('q') => {
            app.screen = Screen::Dashboard;
            vec![]
        }
        KeyCode::Down | KeyCode::Char('j') => {
            if !app.snapshot_entries.is_empty() {
                app.snapshot_selected =
                    (app.snapshot_selected + 1).min(app.snapshot_entries.len() - 1);
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.snapshot_selected > 0 {
                app.snapshot_selected -= 1;
            }
            vec![]
        }
        KeyCode::Enter => {
            if let Some(entry) = app.snapshot_entries.get(app.snapshot_selected) {
                vec![Cmd::RestoreSnapshot {
                    target_id: entry.target_id,
                    save_key: entry.save_key.clone(),
                    timestamp: entry.timestamp.clone(),
                }]
            } else {
                vec![]
            }
        }
        _ => vec![],
    }
}

pub(crate) fn ensure_creds_then(app: &mut App, pending: PendingCmd) -> Vec<Cmd> {
    if app.creds.is_some() {
        run_pending(app, pending)
    } else {
        let needs_webdav = app.needs_webdav_password();
        app.overlay = Overlay::Unlock {
            passphrase: String::new(),
            webdav_password: String::new(),
            needs_webdav,
            field_index: 0,
            error: Option::None,
            then: Some(pending),
            recovery_mode: false,
        };
        vec![]
    }
}

fn handle_unlock_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;
    use crossterm::event::KeyModifiers;

    let Overlay::Unlock {
        ref mut passphrase,
        ref mut webdav_password,
        needs_webdav,
        ref mut field_index,
        ref mut error,
        ref mut recovery_mode,
        ..
    } = app.overlay
    else {
        return vec![];
    };
    let max_field = if needs_webdav { 1 } else { 0 };

    match key.code {
        KeyCode::Esc => {
            passphrase.zeroize();
            webdav_password.zeroize();
            app.overlay = Overlay::None;
            vec![]
        }
        KeyCode::Tab | KeyCode::Down => {
            *field_index = (*field_index + 1).min(max_field);
            vec![]
        }
        KeyCode::BackTab | KeyCode::Up => {
            *field_index = field_index.saturating_sub(1);
            vec![]
        }
        KeyCode::Enter => {
            *error = Option::None;
            if passphrase.is_empty() {
                *error = Some(
                    if *recovery_mode {
                        "Recovery key is required."
                    } else {
                        "Passphrase is required."
                    }
                    .into(),
                );
                return vec![];
            }
            let pass_clone = passphrase.clone();
            let wdav_clone = if needs_webdav && !webdav_password.is_empty() {
                Some(webdav_password.clone())
            } else if needs_webdav {
                *error = Some("WebDAV password is required.".into());
                return vec![];
            } else {
                crate::cli_session::env_webdav_password()
            };
            passphrase.zeroize();
            webdav_password.zeroize();
            vec![Cmd::AttemptUnlock {
                passphrase: pass_clone,
                webdav_password: wdav_clone,
                recovery: *recovery_mode,
            }]
        }
        KeyCode::Char('r') if key.modifiers.contains(KeyModifiers::CONTROL) => {
            *recovery_mode = !*recovery_mode;
            passphrase.zeroize();
            *field_index = 0;
            *error = None;
            vec![]
        }
        KeyCode::Backspace | KeyCode::Char(_) => {
            let field = if *field_index == 0 {
                passphrase
            } else {
                webdav_password
            };
            edit_text(field, key.code);
            vec![]
        }
        _ => vec![],
    }
}

fn handle_form_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Overlay::TargetForm {
        ref mode,
        ref mut name,
        ref mut path,
        ref mut adapter,
        ref mut system,
        ref mut preset,
        ref mut save_dir,
        ref mut field_index,
        ref mut error,
    } = app.overlay
    else {
        return vec![];
    };
    let mode = mode.clone();
    const PRESET_FIELD: usize = 4;
    let preset_count = PRESETS.len() + 1;

    match key.code {
        KeyCode::Esc => {
            app.overlay = Overlay::None;
            vec![]
        }
        KeyCode::Tab | KeyCode::Down => {
            *field_index = (*field_index + 1).min(5);
            vec![]
        }
        KeyCode::BackTab | KeyCode::Up => {
            *field_index = field_index.saturating_sub(1);
            vec![]
        }
        KeyCode::Left | KeyCode::Right if *field_index == PRESET_FIELD => {
            *preset = if key.code == KeyCode::Right {
                (*preset + 1) % preset_count
            } else {
                (*preset + preset_count - 1) % preset_count
            };
            if *preset > 0 {
                *adapter = "rom_keyed".into();
            }
            vec![]
        }
        KeyCode::Enter => {
            let target = SyncTarget {
                name: name.clone(),
                path: path.clone().into(),
                adapter: adapter.clone(),
                system: system.clone(),
                save_dir: (!save_dir.trim().is_empty()).then(|| save_dir.trim().into()),
                preset: preset_at(*preset).map(|p| p.id.to_string()),
            };
            match crate::config::validate_sync_target(&target) {
                Err(e) => {
                    *error = Some(e.to_string());
                    vec![]
                }
                Ok(()) => {
                    match mode {
                        FormMode::Add => {
                            app.targets.push(target);
                            app.selected = app.targets.len() - 1;
                        }
                        FormMode::Edit(idx) => {
                            if idx < app.targets.len() {
                                app.targets[idx] = target;
                                reindex_target_state(app, |i| (i != idx).then_some(i));
                                clamp_row_sel(app);
                            }
                        }
                    }
                    app.dirty = true;
                    app.overlay = Overlay::None;
                    vec![Cmd::SaveConfig]
                }
            }
        }
        KeyCode::Backspace | KeyCode::Char(_) => {
            let field = match field_index {
                0 => Some(name),
                1 => Some(path),
                2 => Some(adapter),
                3 => Some(system),
                5 => Some(save_dir),
                _ => None,
            };
            if let Some(field) = field {
                edit_text(field, key.code);
            }
            vec![]
        }
        _ => vec![],
    }
}

fn handle_confirm_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Overlay::Confirm { ref action, .. } = app.overlay else {
        return vec![];
    };
    match key.code {
        KeyCode::Char('y') | KeyCode::Enter => {
            let action = action.clone();
            app.overlay = Overlay::None;
            match action {
                PendingCmd::DeleteTarget(idx) => {
                    if idx < app.targets.len() {
                        app.targets.remove(idx);
                        reindex_target_state(app, |i| match i.cmp(&idx) {
                            std::cmp::Ordering::Less => Some(i),
                            std::cmp::Ordering::Equal => None,
                            std::cmp::Ordering::Greater => Some(i - 1),
                        });
                        if app.selected >= app.targets.len() && app.selected > 0 {
                            app.selected -= 1;
                        }
                        clamp_row_sel(app);
                        app.dirty = true;
                    }
                    vec![Cmd::SaveConfig]
                }
                PendingCmd::Logout => {
                    app.creds = None;
                    app.push_log("Logged out.".into());
                    vec![Cmd::ClearSession]
                }
                PendingCmd::InstallUpdate(rel) => {
                    app.update_status = Some("Downloading... 0%".into());
                    vec![Cmd::InstallUpdate(rel)]
                }
                other => ensure_creds_then(app, other),
            }
        }
        KeyCode::Char('n') | KeyCode::Esc => {
            app.overlay = Overlay::None;
            vec![]
        }
        _ => vec![],
    }
}

fn handle_setup_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Some(ref mut form) = app.setup else {
        return vec![];
    };
    if form.busy {
        return vec![];
    }
    let max_field: usize = 4;

    match key.code {
        KeyCode::Esc => {
            form.passphrase.zeroize();
            form.password.zeroize();
            form.confirm.zeroize();
            vec![Cmd::Quit]
        }
        KeyCode::Tab | KeyCode::Down => {
            form.field = (form.field + 1).min(max_field);
            vec![]
        }
        KeyCode::BackTab | KeyCode::Up => {
            form.field = form.field.saturating_sub(1);
            vec![]
        }
        KeyCode::Enter => {
            form.error = None;
            if let Err(e) = validate_server_url(&form.server_url) {
                form.error = Some(e.into());
                return vec![];
            }
            if form.passphrase.is_empty() {
                form.error = Some("Passphrase is required.".into());
                return vec![];
            }
            if form.passphrase != form.confirm {
                form.error = Some("Passphrases do not match.".into());
                form.passphrase.zeroize();
                form.confirm.zeroize();
                return vec![];
            }
            let cmd = Cmd::RunSetup {
                server_url: form.server_url.clone(),
                username: form.username.clone(),
                password: form.password.clone(),
                passphrase: form.passphrase.clone(),
            };
            form.passphrase.zeroize();
            form.password.zeroize();
            form.confirm.zeroize();
            form.busy = true;
            vec![cmd]
        }
        KeyCode::Backspace | KeyCode::Char(_) => {
            let field = match form.field {
                0 => &mut form.server_url,
                1 => &mut form.username,
                2 => &mut form.password,
                3 => &mut form.passphrase,
                _ => &mut form.confirm,
            };
            edit_text(field, key.code);
            vec![]
        }
        _ => vec![],
    }
}

fn submit_settings(app: &mut App) -> Vec<Cmd> {
    let Some(ref form) = app.settings else {
        return vec![];
    };
    if let Err(e) = validate_server_url(&form.server_url) {
        if let Some(ref mut f) = app.settings {
            f.error = Some(e.into());
        }
        return vec![];
    }
    let server_changed = form.server_url != app.server_url;
    let username_changed = form.username != app.username.clone().unwrap_or_default();
    let mut new_password = app
        .settings
        .as_mut()
        .map(|f| std::mem::take(&mut f.webdav_password))
        .unwrap_or_default();
    let Some(ref form) = app.settings else {
        return vec![];
    };
    app.server_url = form.server_url.clone();
    app.username = if form.username.is_empty() {
        None
    } else {
        Some(form.username.clone())
    };
    app.conflict_policy = form.conflict_policy;
    app.safety_backup = form.safety_backup;
    let cfg = WaystoneConfig {
        device_id: app.device_id.clone(),
        server_url: app.server_url.clone(),
        conflict_policy: app.conflict_policy,
        username: app.username.clone(),
        targets: app.targets.clone(),
        safety_backup: app.safety_backup,
    };
    let mut cmds = vec![Cmd::SaveSettings(cfg)];
    if server_changed {
        app.creds = None;
        app.statuses.clear();
        app.rows.clear();
        app.conflicts.clear();
        app.conflict_sel = 0;
        app.push_log("Server URL changed -- session invalidated.".into());
        cmds.push(Cmd::ClearSession);
    } else if !new_password.is_empty() && app.creds.is_some() {
        cmds.push(Cmd::RebuildCreds {
            password: std::mem::take(&mut new_password),
        });
    } else if username_changed {
        app.creds = None;
        app.push_log("Username changed -- unlock again with its WebDAV password.".into());
        cmds.push(Cmd::ClearSession);
    } else if !new_password.is_empty() {
        app.push_log("WebDAV password not applied -- unlock first.".into());
    }
    new_password.zeroize();
    app.settings = None;
    app.screen = Screen::Dashboard;
    app.push_log("Settings saved.".into());
    cmds
}

fn handle_settings_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Some(ref mut form) = app.settings else {
        return vec![];
    };
    // Fields: 0=server_url, 1=conflict_policy, 2=safety_backup, 3=username, 4=webdav password,
    // 5=check for updates, 6=log out. Device ID is read-only and not a field index.
    let max_field: usize = SETTINGS_FIELD_LOGOUT;

    match key.code {
        KeyCode::Esc => {
            app.settings = None;
            app.screen = Screen::Dashboard;
            app.push_log("Settings discarded.".into());
            vec![]
        }
        KeyCode::Tab | KeyCode::Down => {
            form.field = (form.field + 1).min(max_field);
            vec![]
        }
        KeyCode::BackTab | KeyCode::Up => {
            form.field = form.field.saturating_sub(1);
            vec![]
        }
        KeyCode::Enter => match form.field {
            SETTINGS_FIELD_UPDATE => {
                app.update_status = Some("Checking for updates...".into());
                vec![Cmd::CheckUpdate]
            }
            SETTINGS_FIELD_LOGOUT => {
                if app.creds.is_some() {
                    app.overlay = Overlay::Confirm {
                        message: "Log out and forget the saved session?".into(),
                        action: PendingCmd::Logout,
                    };
                } else {
                    app.update_status = Some("Not logged in.".into());
                }
                vec![]
            }
            _ => submit_settings(app),
        },
        KeyCode::Char(' ') if form.field == 1 => {
            form.conflict_policy = match form.conflict_policy {
                waystone_core::conflict::ConflictPolicy::NewestWins => {
                    waystone_core::conflict::ConflictPolicy::Prompt
                }
                waystone_core::conflict::ConflictPolicy::Prompt => {
                    waystone_core::conflict::ConflictPolicy::NewestWins
                }
            };
            vec![]
        }
        KeyCode::Char(' ') if form.field == 2 => {
            form.safety_backup = !form.safety_backup;
            vec![]
        }
        KeyCode::Backspace | KeyCode::Char(_) => {
            let field = match form.field {
                0 => Some(&mut form.server_url),
                3 => Some(&mut form.username),
                SETTINGS_FIELD_PASSWORD => Some(&mut form.webdav_password),
                _ => None,
            };
            if let Some(field) = field {
                edit_text(field, key.code);
            }
            vec![]
        }
        _ => vec![],
    }
}

/// Backspace pops, a character appends; other keys are ignored.
fn edit_text(field: &mut String, code: crossterm::event::KeyCode) {
    use crossterm::event::KeyCode;
    match code {
        KeyCode::Backspace => {
            field.pop();
        }
        KeyCode::Char(c) => field.push(c),
        _ => {}
    }
}

fn handle_recovery_key_overlay(app: &mut App, key_event: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let device_id = app.device_id.clone();
    let Overlay::RecoveryKey { ref mut key } = app.overlay else {
        return vec![];
    };

    match key_event.code {
        KeyCode::Char('s') => {
            vec![Cmd::SaveRecoveryFile {
                key: key.clone(),
                device_id,
            }]
        }
        KeyCode::Enter => {
            key.zeroize();
            app.overlay = Overlay::None;
            app.push_log("Recovery key acknowledged.".into());
            vec![]
        }
        _ => vec![],
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};

    fn key(code: KeyCode) -> Msg {
        Msg::Key(KeyEvent::new(code, KeyModifiers::NONE))
    }

    fn test_app() -> App {
        let config = WaystoneConfig {
            device_id: "test-dev".into(),
            server_url: "http://localhost".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: Option::None,
            targets: vec![
                SyncTarget::plain(
                    "Switch JKSV",
                    std::path::Path::new("/saves/jksv"),
                    "jksv",
                    "switch",
                ),
                SyncTarget::plain(
                    "GBA mGBA",
                    std::path::Path::new("/saves/mgba"),
                    "mgba",
                    "gba",
                ),
                SyncTarget::plain(
                    "NDS Twilight",
                    std::path::Path::new("/saves/twilight"),
                    "twilight",
                    "nds",
                ),
            ],
            safety_backup: true,
        };
        App::new(&config)
    }

    fn make_conflict_entry(target_id: usize) -> ConflictEntry {
        ConflictEntry {
            target_id,
            label: "Switch \u{00b7} Test/main".to_string(),
            save_key: format!("switch/TEST_{}/main", target_id),
            local: HeadInfo {
                hash: "local_hash".into(),
                mtime: "2026-01-01T00:00:00Z".into(),
                device_id: None,
            },
            remote: HeadInfo {
                hash: "remote_hash".into(),
                mtime: "2026-01-02T00:00:00Z".into(),
                device_id: Some("other-dev".into()),
            },
            remote_hash: "remote_hash".into(),
            head_mtime: "2026-01-02T00:00:00Z".into(),
            queued: false,
        }
    }

    #[test]
    fn selection_moves_down() {
        let mut app = test_app();
        assert_eq!(app.selected, 0);
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.selected, 1);
        update(&mut app, key(KeyCode::Down));
        assert_eq!(app.selected, 2);
    }

    #[test]
    fn selection_clamps_at_bounds() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.selected, 0);

        app.selected = 2;
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.selected, 2);
    }

    #[test]
    fn reorder_down_swaps_and_moves_cursor() {
        let mut app = test_app();
        app.selected = 0;
        update(&mut app, key(KeyCode::Char('J')));
        assert_eq!(app.selected, 1);
        assert_eq!(app.targets[0].name, "GBA mGBA");
        assert_eq!(app.targets[1].name, "Switch JKSV");
        assert!(app.dirty);
    }

    #[test]
    fn reorder_up_swaps_and_moves_cursor() {
        let mut app = test_app();
        app.selected = 1;
        update(&mut app, key(KeyCode::Char('K')));
        assert_eq!(app.selected, 0);
        assert_eq!(app.targets[0].name, "GBA mGBA");
        assert_eq!(app.targets[1].name, "Switch JKSV");
        assert!(app.dirty);
    }

    #[test]
    fn add_target_opens_form_overlay() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('a')));
        assert!(matches!(
            app.overlay,
            Overlay::TargetForm {
                mode: FormMode::Add,
                ..
            }
        ));
    }

    #[test]
    fn edit_target_opens_form_with_current_values() {
        let mut app = test_app();
        app.selected = 1;
        update(&mut app, key(KeyCode::Char('e')));
        if let Overlay::TargetForm { name, adapter, .. } = &app.overlay {
            assert_eq!(name, "GBA mGBA");
            assert_eq!(adapter, "mgba");
        } else {
            panic!("expected TargetForm overlay");
        }
    }

    #[test]
    fn delete_target_shows_confirm_then_removes() {
        let mut app = test_app();
        app.selected = 1;
        update(&mut app, key(KeyCode::Char('d')));
        assert!(matches!(app.overlay, Overlay::Confirm { .. }));

        let cmds = update(&mut app, key(KeyCode::Char('y')));
        assert_eq!(app.targets.len(), 2);
        assert_eq!(app.targets[0].name, "Switch JKSV");
        assert_eq!(app.targets[1].name, "NDS Twilight");
        assert!(app.dirty);
        assert!(cmds.iter().any(|c| matches!(c, Cmd::SaveConfig)));
    }

    #[test]
    fn confirm_cancel_closes_overlay() {
        let mut app = test_app();
        app.overlay = Overlay::Confirm {
            message: "test".into(),
            action: PendingCmd::DeleteTarget(0),
        };
        update(&mut app, key(KeyCode::Esc));
        assert!(matches!(app.overlay, Overlay::None));
        assert_eq!(app.targets.len(), 3);
    }

    #[test]
    fn help_overlay_closes_on_any_key() {
        let mut app = test_app();
        app.overlay = Overlay::Help;
        update(&mut app, key(KeyCode::Char('x')));
        assert!(matches!(app.overlay, Overlay::None));
    }

    #[test]
    fn push_without_creds_opens_unlock() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('p')));
        assert!(matches!(app.overlay, Overlay::Unlock { .. }));
    }

    #[test]
    fn action_done_clears_busy_and_logs() {
        let mut app = test_app();
        app.busy = Some(0);
        update(
            &mut app,
            Msg::ActionDone {
                target_id: 0,
                result: ActionResult::Ok("pushed 3 saves".into()),
            },
        );
        assert!(app.busy.is_none());
        assert!(app.log.back().unwrap().text.contains("pushed 3 saves"));
    }

    #[test]
    fn action_done_error_sets_target_status() {
        let mut app = test_app();
        app.busy = Some(1);
        update(
            &mut app,
            Msg::ActionDone {
                target_id: 1,
                result: ActionResult::Err("connection refused".into()),
            },
        );
        assert!(app.busy.is_none());
        assert!(matches!(app.statuses.get(&1), Some(TargetStatus::Error(_))));
    }

    #[test]
    fn status_msg_updates_target_status() {
        let mut app = test_app();
        update(
            &mut app,
            Msg::Status {
                target_id: 0,
                status: TargetStatus::InSync,
            },
        );
        assert_eq!(app.statuses.get(&0), Some(&TargetStatus::InSync));
    }

    #[test]
    fn progress_msg_adds_log_line() {
        let mut app = test_app();
        update(
            &mut app,
            Msg::Progress {
                target_id: 0,
                phase: "encrypting".into(),
            },
        );
        assert!(app.log.back().unwrap().text.contains("encrypting"));
    }

    #[test]
    fn tick_increments_spinner() {
        let mut app = test_app();
        let s0 = app.spinner;
        update(&mut app, Msg::Tick);
        assert_eq!(app.spinner, s0 + 1);
    }

    #[test]
    fn quit_key_emits_quit_cmd() {
        let mut app = test_app();
        let cmds = update(&mut app, key(KeyCode::Char('q')));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::Quit)));
    }

    #[test]
    fn keys_blocked_while_busy_except_quit() {
        let mut app = test_app();
        app.busy = Some(0);
        let cmds = update(&mut app, key(KeyCode::Char('p')));
        assert!(cmds.is_empty());
        let cmds = update(&mut app, key(KeyCode::Char('q')));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::Quit)));
    }

    #[test]
    fn log_bounded_at_max() {
        let mut app = test_app();
        for i in 0..250 {
            app.push_log(format!("line {}", i));
        }
        assert_eq!(app.log.len(), MAX_LOG_LINES);
    }

    #[test]
    fn delete_last_target_adjusts_selection() {
        let mut app = test_app();
        app.selected = 2;
        app.overlay = Overlay::Confirm {
            message: "delete".into(),
            action: PendingCmd::DeleteTarget(2),
        };
        update(&mut app, key(KeyCode::Char('y')));
        assert_eq!(app.targets.len(), 2);
        assert_eq!(app.selected, 1);
    }

    // --- Conflict screen tests ---

    #[test]
    fn screen_switch_to_conflicts() {
        let mut app = test_app();
        app.conflicts.push(make_conflict_entry(0));
        update(&mut app, key(KeyCode::Char('C')));
        assert_eq!(app.screen, Screen::Conflicts);
        assert_eq!(app.conflict_sel, 0);
    }

    #[test]
    fn conflict_sel_nav_clamps() {
        let mut app = test_app();
        app.screen = Screen::Conflicts;
        app.conflicts = vec![make_conflict_entry(0), make_conflict_entry(1)];

        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.conflict_sel, 1);
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.conflict_sel, 1);

        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.conflict_sel, 0);
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.conflict_sel, 0);
    }

    #[test]
    fn conflicts_esc_returns_to_dashboard() {
        let mut app = test_app();
        app.screen = Screen::Conflicts;
        update(&mut app, key(KeyCode::Esc));
        assert_eq!(app.screen, Screen::Dashboard);
    }

    #[test]
    fn keep_local_without_creds_opens_unlock() {
        let mut app = test_app();
        app.screen = Screen::Conflicts;
        app.conflicts.push(make_conflict_entry(0));
        update(&mut app, key(KeyCode::Char('l')));
        assert!(matches!(app.overlay, Overlay::Unlock { .. }));
    }

    #[test]
    fn conflicts_msg_merges_by_target_id() {
        let mut app = test_app();
        app.conflicts = vec![make_conflict_entry(0), make_conflict_entry(1)];

        let new_entry = ConflictEntry {
            target_id: 0,
            label: "refreshed".into(),
            save_key: "k1_new".into(),
            local: HeadInfo {
                hash: "l2".into(),
                mtime: "t2".into(),
                device_id: None,
            },
            remote: HeadInfo {
                hash: "r2".into(),
                mtime: "t2".into(),
                device_id: Some("d2".into()),
            },
            remote_hash: "r2".into(),
            head_mtime: "t2".into(),
            queued: false,
        };
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target,
                rows: vec![],
                conflicts: vec![new_entry],
            },
        );

        assert_eq!(app.conflicts.len(), 2);
        assert_eq!(app.conflicts.iter().filter(|c| c.target_id == 0).count(), 1);
        assert_eq!(
            app.conflicts
                .iter()
                .find(|c| c.target_id == 0)
                .unwrap()
                .label,
            "refreshed"
        );
        assert_eq!(
            app.conflicts
                .iter()
                .find(|c| c.target_id == 1)
                .unwrap()
                .label,
            "Switch \u{00b7} Test/main"
        );
    }

    #[test]
    fn conflicts_msg_clears_stale_entries() {
        let mut app = test_app();
        app.conflicts = vec![make_conflict_entry(0)];

        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target,
                rows: vec![],
                conflicts: vec![],
            },
        );

        assert!(app.conflicts.is_empty());
    }

    #[test]
    fn refresh_keeps_a_queued_conflict_queued() {
        let mut app = test_app();
        let mut queued = make_conflict_entry(0);
        queued.queued = true;
        app.conflicts = vec![queued];
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target,
                rows: vec![],
                conflicts: vec![make_conflict_entry(0)],
            },
        );
        assert_eq!(app.conflicts.len(), 1);
        assert!(app.conflicts[0].queued);
    }

    #[test]
    fn stale_results_follow_their_target_after_a_reorder_or_are_dropped() {
        let mut app = test_app();
        let first = app.targets[0].clone();
        app.targets.swap(0, 1);
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target: first.clone(),
                rows: vec![row(0, "nds/A/battery", false)],
                conflicts: vec![make_conflict_entry(0)],
            },
        );
        assert!(app.rows_for(0).is_empty());
        assert_eq!(app.rows_for(1).len(), 1);
        assert!(app.conflicts.iter().all(|c| c.target_id == 1));

        update(
            &mut app,
            Msg::History {
                target_id: 0,
                target: first.clone(),
                entries: vec![make_history_view(0, 0)],
            },
        );
        assert_eq!(app.history_entries[0].target_id, 1);

        app.targets.retain(|t| *t != first);
        app.screen = Screen::Dashboard;
        update(
            &mut app,
            Msg::Snapshots {
                target_id: 1,
                target: first,
                entries: vec![make_snapshot_view(1, 0)],
            },
        );
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.snapshot_entries.is_empty());
    }

    fn setup_config() -> WaystoneConfig {
        WaystoneConfig {
            device_id: "test-dev".into(),
            server_url: String::new(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: Option::None,
            targets: vec![],
            safety_backup: true,
        }
    }

    #[test]
    fn first_run_routes_to_setup_screen() {
        let config = setup_config();
        let app = App::new(&config);
        assert_eq!(app.screen, Screen::Setup);
        assert!(app.setup.is_some());
    }

    #[test]
    fn non_empty_server_url_routes_to_dashboard() {
        let app = test_app();
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.setup.is_none());
    }

    #[test]
    fn setup_decision_init_when_no_keys() {
        assert_eq!(setup_decision(false), SetupDecision::Init);
    }

    #[test]
    fn setup_decision_redirect_when_keys_exist() {
        assert_eq!(setup_decision(true), SetupDecision::RedirectUnlock);
    }

    #[test]
    fn setup_validation_rejects_empty_server() {
        let mut app = App::new(&setup_config());
        if let Some(ref mut form) = app.setup {
            form.passphrase = "test123".into();
            form.confirm = "test123".into();
        }
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.is_empty());
        assert!(app.setup.as_ref().unwrap().error.is_some());
        assert!(
            app.setup
                .as_ref()
                .unwrap()
                .error
                .as_ref()
                .unwrap()
                .contains("Server URL")
        );
    }

    #[test]
    fn setup_validation_rejects_mismatched_passphrase() {
        let mut app = App::new(&setup_config());
        if let Some(ref mut form) = app.setup {
            form.server_url = "http://localhost".into();
            form.passphrase = "abc".into();
            form.confirm = "xyz".into();
        }
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.is_empty());
        assert!(
            app.setup
                .as_ref()
                .unwrap()
                .error
                .as_ref()
                .unwrap()
                .contains("do not match")
        );
    }

    #[test]
    fn setup_valid_form_emits_run_setup() {
        let mut app = App::new(&setup_config());
        if let Some(ref mut form) = app.setup {
            form.server_url = "http://localhost:5000".into();
            form.username = "alice".into();
            form.password = "secret".into();
            form.passphrase = "pass123".into();
            form.confirm = "pass123".into();
        }
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::RunSetup { .. })));
        assert!(app.setup.as_ref().unwrap().busy);
    }

    #[test]
    fn setup_result_ok_transitions_to_dashboard_with_recovery_overlay() {
        let mut app = App::new(&setup_config());
        if let Some(ref mut form) = app.setup {
            form.server_url = "http://localhost:5000".into();
        }
        let (vault, _) = waystone_core::crypto::Vault::init("test").unwrap();
        let dav = crate::webdav::WebDavClient::new("http://localhost", None, None);
        let cmds = update(
            &mut app,
            Msg::SetupResult(Ok(SetupOk {
                recovery_key: "deadbeef".into(),
                vault: Arc::new(vault),
                dav: Arc::new(dav),
                blocking_dav: Arc::new(BlockingWebDav::new("http://localhost", None, None)),
            })),
        );
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.setup.is_none());
        assert!(app.creds.is_some());
        assert!(matches!(app.overlay, Overlay::RecoveryKey { .. }));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::SaveConfig)));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::RefreshAllStatuses)));
    }

    #[test]
    fn setup_result_err_keeps_form_with_error() {
        let mut app = App::new(&setup_config());
        if let Some(ref mut form) = app.setup {
            form.busy = true;
        }
        update(
            &mut app,
            Msg::SetupResult(Err("vault already exists".into())),
        );
        assert_eq!(app.screen, Screen::Setup);
        assert!(!app.setup.as_ref().unwrap().busy);
        assert!(
            app.setup
                .as_ref()
                .unwrap()
                .error
                .as_ref()
                .unwrap()
                .contains("vault already exists")
        );
    }

    #[test]
    fn recovery_key_enter_dismisses() {
        let mut app = test_app();
        app.overlay = Overlay::RecoveryKey {
            key: "abcdef1234567890".into(),
        };
        update(&mut app, key(KeyCode::Enter));
        assert!(matches!(app.overlay, Overlay::None));
    }

    #[test]
    fn recovery_key_s_emits_save_cmd() {
        let mut app = test_app();
        app.overlay = Overlay::RecoveryKey {
            key: "abcdef1234567890".into(),
        };
        let cmds = update(&mut app, key(KeyCode::Char('s')));
        assert!(
            cmds.iter()
                .any(|c| matches!(c, Cmd::SaveRecoveryFile { .. }))
        );
    }

    #[test]
    fn settings_opens_from_dashboard_with_s() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('S')));
        assert_eq!(app.screen, Screen::Settings);
        assert!(app.settings.is_some());
        assert_eq!(
            app.settings.as_ref().unwrap().server_url,
            "http://localhost"
        );
    }

    #[test]
    fn settings_esc_discards_and_returns_to_dashboard() {
        let mut app = test_app();
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://changed".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: "bob".into(),
            webdav_password: String::new(),
            safety_backup: true,
            field: 0,
            error: None,
        });
        update(&mut app, key(KeyCode::Esc));
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.settings.is_none());
        assert_eq!(app.server_url, "http://localhost");
    }

    #[test]
    fn settings_save_emits_save_settings_cmd() {
        let mut app = test_app();
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://localhost".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::Prompt,
            username: "alice".into(),
            webdav_password: String::new(),
            safety_backup: true,
            field: 0,
            error: None,
        });
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::SaveSettings(_))));
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.settings.is_none());
    }

    #[test]
    fn settings_server_change_clears_creds() {
        let mut app = test_app();
        let (vault, _) = waystone_core::crypto::Vault::init("test").unwrap();
        let dav = crate::webdav::WebDavClient::new("http://localhost", None, None);
        app.creds = Some(SessionCreds {
            vault: Arc::new(vault),
            dav: Arc::new(dav),
            blocking_dav: Arc::new(BlockingWebDav::new("http://localhost", None, None)),
        });
        app.statuses.insert(0, TargetStatus::InSync);
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://different-server".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: String::new(),
            webdav_password: String::new(),
            safety_backup: true,
            field: 0,
            error: None,
        });
        update(&mut app, key(KeyCode::Enter));
        assert!(app.creds.is_none());
        assert!(app.statuses.is_empty());
        assert_eq!(app.server_url, "http://different-server");
    }

    #[test]
    fn settings_toggle_conflict_policy() {
        let mut app = test_app();
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://localhost".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: String::new(),
            webdav_password: String::new(),
            safety_backup: true,
            field: 1,
            error: None,
        });
        update(&mut app, key(KeyCode::Char(' ')));
        assert_eq!(
            app.settings.as_ref().unwrap().conflict_policy,
            waystone_core::conflict::ConflictPolicy::Prompt
        );
        update(&mut app, key(KeyCode::Char(' ')));
        assert_eq!(
            app.settings.as_ref().unwrap().conflict_policy,
            waystone_core::conflict::ConflictPolicy::NewestWins
        );
    }

    #[test]
    fn settings_form_includes_safety_backup() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('S')));
        assert!(app.settings.as_ref().unwrap().safety_backup);
    }

    #[test]
    fn settings_toggle_safety_backup() {
        let mut app = test_app();
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://localhost".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: String::new(),
            webdav_password: String::new(),
            safety_backup: true,
            field: 2,
            error: None,
        });
        update(&mut app, key(KeyCode::Char(' ')));
        assert!(!app.settings.as_ref().unwrap().safety_backup);
        update(&mut app, key(KeyCode::Char(' ')));
        assert!(app.settings.as_ref().unwrap().safety_backup);
    }

    #[test]
    fn settings_save_carries_safety_backup_false() {
        let mut app = test_app();
        app.screen = Screen::Settings;
        app.settings = Some(SettingsForm {
            server_url: "http://localhost".into(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: String::new(),
            webdav_password: String::new(),
            safety_backup: false,
            field: 0,
            error: None,
        });
        let cmds = update(&mut app, key(KeyCode::Enter));
        let save_cmd = cmds
            .iter()
            .find_map(|c| {
                if let Cmd::SaveSettings(cfg) = c {
                    Some(cfg)
                } else {
                    None
                }
            })
            .expect("should emit SaveSettings");
        assert!(!save_cmd.safety_backup);
    }

    #[test]
    fn setup_esc_quits_on_first_run() {
        let mut app = App::new(&setup_config());
        let cmds = update(&mut app, key(KeyCode::Esc));
        assert!(cmds.iter().any(|c| matches!(c, Cmd::Quit)));
    }

    // --- History screen tests ---
    fn make_history_view(target_id: usize, idx: usize) -> HistoryView {
        HistoryView {
            target_id,
            save_key: format!("switch/GAME_{}/main", idx),
            label: format!("Target {} \u{00b7} Game_{}/main", target_id, idx),
            timestamp: format!("20260907T14310{}Z", idx),
            device_id: "dev1".into(),
            hash: format!("hash_{:064}", idx),
            mtime: format!("2026-09-07T14:31:0{}Z", idx),
        }
    }

    #[test]
    fn h_key_from_dashboard_emits_load_history() {
        let mut app = test_app();
        let (vault, _) = waystone_core::crypto::Vault::init("test").unwrap();
        let dav = crate::webdav::WebDavClient::new("http://localhost", None, None);
        app.creds = Some(SessionCreds {
            vault: Arc::new(vault),
            dav: Arc::new(dav),
            blocking_dav: Arc::new(BlockingWebDav::new("http://localhost", None, None)),
        });
        let cmds = update(&mut app, key(KeyCode::Char('h')));
        assert!(
            cmds.iter()
                .any(|c| matches!(c, Cmd::LoadHistory { target_id: 0 }))
        );
    }

    #[test]
    fn h_key_without_creds_opens_unlock() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('h')));
        assert!(matches!(app.overlay, Overlay::Unlock { .. }));
    }

    #[test]
    fn history_msg_populates_entries_and_switches_screen() {
        let mut app = test_app();
        let entries = vec![make_history_view(0, 0), make_history_view(0, 1)];
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::History {
                target_id: 0,
                target,
                entries: entries.clone(),
            },
        );
        assert_eq!(app.screen, Screen::History);
        assert_eq!(app.history_entries.len(), 2);
        assert_eq!(app.history_selected, 0);
    }

    #[test]
    fn history_nav_clamps() {
        let mut app = test_app();
        app.screen = Screen::History;
        app.history_entries = vec![make_history_view(0, 0), make_history_view(0, 1)];
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.history_selected, 1);
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.history_selected, 1);
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.history_selected, 0);
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.history_selected, 0);
    }

    #[test]
    fn history_esc_returns_to_dashboard() {
        let mut app = test_app();
        app.screen = Screen::History;
        update(&mut app, key(KeyCode::Esc));
        assert_eq!(app.screen, Screen::Dashboard);
    }

    #[test]
    fn history_enter_emits_restore_cmd() {
        let mut app = test_app();
        app.screen = Screen::History;
        let (vault, _) = waystone_core::crypto::Vault::init("test").unwrap();
        let dav = crate::webdav::WebDavClient::new("http://localhost", None, None);
        app.creds = Some(SessionCreds {
            vault: Arc::new(vault),
            dav: Arc::new(dav),
            blocking_dav: Arc::new(BlockingWebDav::new("http://localhost", None, None)),
        });
        app.history_entries = vec![make_history_view(0, 0)];
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.iter().any(|c| matches!(
            c,
            Cmd::RestoreHistory { target_id: 0, save_key, hash }
            if save_key == "switch/GAME_0/main" && *hash == format!("hash_{:064}", 0)
        )));
    }

    #[test]
    fn history_empty_msg_stays_on_history_screen() {
        let mut app = test_app();
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::History {
                target_id: 0,
                target,
                entries: vec![],
            },
        );
        assert_eq!(app.screen, Screen::History);
        assert!(app.history_entries.is_empty());
    }

    // --- Snapshots screen tests ---
    fn make_snapshot_view(target_id: usize, idx: usize) -> SnapshotView {
        SnapshotView {
            target_id,
            save_key: format!("switch/GAME_{}/main", idx),
            label: format!("Target {} \u{00b7} Game_{}/main", target_id, idx),
            timestamp: format!("20260907T14310{}.000Z", idx),
            file_count: 3,
            total_bytes: 1024,
        }
    }

    #[test]
    fn b_key_from_dashboard_emits_load_snapshots_directly() {
        let mut app = test_app();
        assert!(app.creds.is_none());
        let cmds = update(&mut app, key(KeyCode::Char('b')));
        assert!(
            cmds.iter()
                .any(|c| matches!(c, Cmd::LoadSnapshots { target_id: 0 }))
        );
        assert!(matches!(app.overlay, Overlay::None));
    }

    #[test]
    fn snapshots_msg_populates_entries_and_switches_screen() {
        let mut app = test_app();
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Snapshots {
                target_id: 0,
                target,
                entries: vec![make_snapshot_view(0, 0), make_snapshot_view(0, 1)],
            },
        );
        assert_eq!(app.screen, Screen::Snapshots);
        assert_eq!(app.snapshot_entries.len(), 2);
        assert_eq!(app.snapshot_selected, 0);
    }

    #[test]
    fn snapshots_nav_clamps() {
        let mut app = test_app();
        app.screen = Screen::Snapshots;
        app.snapshot_entries = vec![make_snapshot_view(0, 0), make_snapshot_view(0, 1)];
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.snapshot_selected, 1);
        update(&mut app, key(KeyCode::Char('j')));
        assert_eq!(app.snapshot_selected, 1);
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.snapshot_selected, 0);
        update(&mut app, key(KeyCode::Char('k')));
        assert_eq!(app.snapshot_selected, 0);
    }

    #[test]
    fn snapshots_esc_returns_to_dashboard() {
        let mut app = test_app();
        app.screen = Screen::Snapshots;
        update(&mut app, key(KeyCode::Esc));
        assert_eq!(app.screen, Screen::Dashboard);
    }

    #[test]
    fn snapshots_enter_emits_restore_cmd() {
        let mut app = test_app();
        app.screen = Screen::Snapshots;
        app.snapshot_entries = vec![make_snapshot_view(0, 0)];
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.iter().any(|c| matches!(
            c,
            Cmd::RestoreSnapshot { target_id: 0, save_key, timestamp }
            if save_key == "switch/GAME_0/main" && timestamp == "20260907T143100.000Z"
        )));
    }

    #[test]
    fn snapshots_empty_msg_stays_on_snapshots_screen() {
        let mut app = test_app();
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Snapshots {
                target_id: 0,
                target,
                entries: vec![],
            },
        );
        assert_eq!(app.screen, Screen::Snapshots);
        assert!(app.snapshot_entries.is_empty());
    }

    // --- Recovery-mode unlock tests ---

    fn key_ctrl(c: char) -> Msg {
        Msg::Key(KeyEvent::new(KeyCode::Char(c), KeyModifiers::CONTROL))
    }

    fn app_with_unlock_overlay() -> App {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('p')));
        assert!(matches!(app.overlay, Overlay::Unlock { .. }));
        app
    }

    #[test]
    fn ctrl_r_toggles_recovery_mode() {
        let mut app = app_with_unlock_overlay();
        // type 2 chars into passphrase first
        update(&mut app, key(KeyCode::Char('x')));
        update(&mut app, key(KeyCode::Char('y')));
        // toggle to recovery mode — passphrase should be cleared
        update(&mut app, key_ctrl('r'));
        let Overlay::Unlock {
            ref recovery_mode,
            ref passphrase,
            ..
        } = app.overlay
        else {
            panic!("expected Unlock overlay");
        };
        assert!(*recovery_mode, "recovery_mode should be true after Ctrl+R");
        assert!(passphrase.is_empty(), "passphrase cleared on toggle");
        // toggle back to passphrase mode
        update(&mut app, key_ctrl('r'));
        let Overlay::Unlock {
            ref recovery_mode, ..
        } = app.overlay
        else {
            panic!("expected Unlock overlay");
        };
        assert!(
            !*recovery_mode,
            "recovery_mode should be false after second Ctrl+R"
        );
    }

    #[test]
    fn unlock_enter_recovery_mode_emits_attempt_unlock_recovery_true() {
        let mut app = app_with_unlock_overlay();
        update(&mut app, key_ctrl('r'));
        // type a char into the secret (field 0, holds recovery key in this mode)
        update(&mut app, key(KeyCode::Char('k')));
        // fill webdav field too if needed (Tab + char)
        update(&mut app, key(KeyCode::Tab));
        update(&mut app, key(KeyCode::Char('w')));
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(
            cmds.iter()
                .any(|c| matches!(c, Cmd::AttemptUnlock { recovery: true, .. })),
            "should emit AttemptUnlock with recovery: true"
        );
    }

    #[test]
    fn unlock_enter_passphrase_mode_recovery_false() {
        let mut app = app_with_unlock_overlay();
        // type a passphrase char (field 0)
        update(&mut app, key(KeyCode::Char('p')));
        // fill webdav field too if needed
        update(&mut app, key(KeyCode::Tab));
        update(&mut app, key(KeyCode::Char('w')));
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(
            cmds.iter().any(|c| matches!(
                c,
                Cmd::AttemptUnlock {
                    recovery: false,
                    ..
                }
            )),
            "should emit AttemptUnlock with recovery: false"
        );
    }

    #[test]
    fn unlock_recovery_empty_shows_recovery_required_error() {
        let mut app = app_with_unlock_overlay();
        update(&mut app, key_ctrl('r'));
        // Enter with empty secret
        update(&mut app, key(KeyCode::Enter));
        let Overlay::Unlock { ref error, .. } = app.overlay else {
            panic!("expected Unlock overlay");
        };
        assert!(
            error
                .as_ref()
                .map(|e| e.contains("Recovery key"))
                .unwrap_or(false),
            "error should mention Recovery key"
        );
    }

    fn row(target_id: usize, key: &str, remote_only: bool) -> SaveRow {
        SaveRow {
            target_id,
            group_key: key.into(),
            name: key.into(),
            slot: "battery".into(),
            status: TargetStatus::InSync,
            remote_only,
        }
    }

    fn test_app_with_creds() -> App {
        let mut app = test_app();
        let blocking =
            std::thread::spawn(|| Arc::new(BlockingWebDav::new("http://localhost:1", None, None)))
                .join()
                .unwrap();
        app.creds = Some(SessionCreds {
            vault: Arc::new(Vault::from_mdk([7; 32])),
            dav: Arc::new(WebDavClient::new("http://localhost:1", None, None)),
            blocking_dav: blocking,
        });
        app
    }

    #[test]
    fn moving_a_target_moves_its_rows_and_status() {
        let mut app = test_app();
        app.rows = vec![row(0, "a", false), row(1, "b", false)];
        app.statuses.insert(0, TargetStatus::Ahead);
        update(&mut app, key(KeyCode::Char('J')));
        assert_eq!(app.selected, 1);
        assert_eq!(app.targets[1].name, "Switch JKSV");
        assert_eq!(app.rows_for(1)[0].group_key, "a");
        assert_eq!(app.rows_for(0)[0].group_key, "b");
        assert_eq!(app.statuses.get(&1), Some(&TargetStatus::Ahead));
        assert!(!app.statuses.contains_key(&0));
    }

    #[test]
    fn deleting_a_target_drops_its_rows_and_shifts_later_ones() {
        let mut app = test_app();
        app.rows = vec![row(0, "a", false), row(1, "b", false), row(2, "c", false)];
        app.statuses.insert(2, TargetStatus::Behind);
        app.overlay = Overlay::Confirm {
            message: String::new(),
            action: PendingCmd::DeleteTarget(1),
        };
        update(&mut app, key(KeyCode::Char('y')));
        let keys: Vec<_> = app
            .rows
            .iter()
            .map(|r| (r.target_id, r.group_key.as_str()))
            .collect();
        assert_eq!(keys, vec![(0, "a"), (1, "c")]);
        assert_eq!(app.statuses.get(&1), Some(&TargetStatus::Behind));
    }

    #[test]
    fn tab_toggles_pane_and_s_in_saves_pane_syncs_selected_save() {
        let mut app = test_app_with_creds();
        app.rows = vec![
            row(0, "nds/A/battery", false),
            row(0, "nds/B/battery", false),
        ];
        update(&mut app, key(KeyCode::Tab));
        assert_eq!(app.pane, Pane::Saves);
        update(&mut app, key(KeyCode::Down));
        let cmds = update(&mut app, key(KeyCode::Char('s')));
        assert!(matches!(
            &cmds[..],
            [Cmd::StartSync { selection: Selection::Save { target: 0, group_key }, mode: RunMode::Sync }]
                if group_key == "nds/B/battery"
        ));
        assert_eq!(app.busy, Some(SYNC_BUSY));
    }

    #[test]
    fn push_on_remote_only_row_is_refused_but_pull_is_allowed() {
        let mut app = test_app_with_creds();
        app.rows = vec![row(0, "nds/A/slot-1", true)];
        app.pane = Pane::Saves;
        assert!(update(&mut app, key(KeyCode::Char('p'))).is_empty());
        assert!(
            app.log
                .back()
                .unwrap()
                .text
                .contains("Nothing local to upload")
        );
        let cmds = update(&mut app, key(KeyCode::Char('P')));
        assert!(matches!(
            &cmds[..],
            [Cmd::StartSync {
                mode: RunMode::Pull,
                ..
            }]
        ));
    }

    #[test]
    fn target_pane_keys_map_to_target_selection_modes() {
        let mut app = test_app_with_creds();
        app.selected = 1;
        for (c, mode) in [
            ('s', RunMode::Sync),
            ('p', RunMode::Push),
            ('P', RunMode::Pull),
        ] {
            app.busy = None;
            let cmds = update(&mut app, key(KeyCode::Char(c)));
            assert!(matches!(
                &cmds[..],
                [Cmd::StartSync { selection: Selection::Target(1), mode: m }] if *m == mode
            ));
        }
    }

    #[test]
    fn a_syncs_all_and_without_creds_opens_unlock_with_pending_sync_all() {
        let mut app = test_app_with_creds();
        let cmds = update(&mut app, key(KeyCode::Char('A')));
        assert!(matches!(
            &cmds[..],
            [Cmd::StartSync {
                selection: Selection::All,
                mode: RunMode::Sync
            }]
        ));
        let mut app = test_app();
        assert!(update(&mut app, key(KeyCode::Char('A'))).is_empty());
        assert!(matches!(
            &app.overlay,
            Overlay::Unlock {
                then: Some(PendingCmd::Sync {
                    selection: Selection::All,
                    ..
                }),
                ..
            }
        ));
    }

    #[test]
    fn sync_progress_messages_drive_the_overlay_and_cancel() {
        let mut app = test_app_with_creds();
        update(&mut app, Msg::SyncStarted { total: 2 });
        update(
            &mut app,
            Msg::SyncItem {
                index: 0,
                total: 2,
                name: "Zelda".into(),
                phase: SyncPhase::Pulling,
            },
        );
        assert_eq!(app.sync_run.as_ref().unwrap().ratio(), 0.5);
        update(&mut app, key(KeyCode::Char('c')));
        assert!(app.sync_cancel.load(Ordering::SeqCst));
        assert!(app.sync_run.as_ref().unwrap().cancelling);
        app.busy = Some(SYNC_BUSY);
        update(
            &mut app,
            Msg::SyncFinished(SyncSummary {
                pulled: 1,
                cancelled: true,
                ..Default::default()
            }),
        );
        assert!(app.busy.is_none());
        assert!(!app.sync_run.as_ref().unwrap().running());
        assert_eq!(app.sync_run.as_ref().unwrap().ratio(), 1.0);
        assert_eq!(update(&mut app, key(KeyCode::Enter)).len(), 1);
        assert!(app.sync_run.is_none());
    }

    #[test]
    fn cancel_while_planning_survives_sync_started() {
        let mut app = test_app_with_creds();
        app.busy = Some(SYNC_BUSY);
        assert!(app.sync_starting());
        update(&mut app, key(KeyCode::Char('c')));
        assert!(app.sync_cancel.load(Ordering::SeqCst));
        update(&mut app, Msg::SyncStarted { total: 1 });
        assert!(app.sync_cancel.load(Ordering::SeqCst));
    }

    #[test]
    fn sync_finished_without_started_still_shows_summary() {
        let mut app = test_app_with_creds();
        app.busy = Some(SYNC_BUSY);
        update(
            &mut app,
            Msg::SyncFinished(SyncSummary {
                errors: 1,
                error_messages: vec!["boom".into()],
                ..Default::default()
            }),
        );
        assert!(app.busy.is_none());
        assert!(app.sync_run.as_ref().is_some_and(|r| !r.running()));
        assert!(app.log.iter().any(|l| l.text.contains("boom")));
    }

    #[test]
    fn rows_message_sets_rows_conflicts_and_worst_status() {
        let mut app = test_app();
        let mut behind = row(0, "nds/B/battery", false);
        behind.status = TargetStatus::Behind;
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target,
                rows: vec![row(0, "nds/A/battery", false), behind],
                conflicts: vec![make_conflict_entry(0)],
            },
        );
        assert_eq!(app.statuses[&0], TargetStatus::Behind);
        assert_eq!(app.rows_for(0).len(), 2);
        assert_eq!(app.conflicts.len(), 1);
        let target = app.targets[0].clone();
        update(
            &mut app,
            Msg::Rows {
                target_id: 0,
                target,
                rows: vec![row(0, "nds/A/battery", false)],
                conflicts: vec![],
            },
        );
        assert_eq!(app.statuses[&0], TargetStatus::InSync);
        assert_eq!(app.rows_for(0).len(), 1);
        assert!(app.conflicts.is_empty());
    }

    #[test]
    fn form_preset_cycles_and_forces_rom_keyed() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('a')));
        for _ in 0..4 {
            update(&mut app, key(KeyCode::Tab));
        }
        update(&mut app, key(KeyCode::Right));
        let Overlay::TargetForm {
            preset, adapter, ..
        } = &app.overlay
        else {
            panic!("form expected");
        };
        assert_eq!((*preset, adapter.as_str()), (1, "rom_keyed"));
        assert_eq!(preset_label(*preset), "RetroArch");
    }

    #[test]
    fn form_retroarch_without_save_dir_shows_validation_error() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('a')));
        for c in "Roms".chars() {
            update(&mut app, key(KeyCode::Char(c)));
        }
        for _ in 0..4 {
            update(&mut app, key(KeyCode::Tab));
        }
        update(&mut app, key(KeyCode::Right));
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.is_empty());
        let Overlay::TargetForm { error, .. } = &app.overlay else {
            panic!("form expected");
        };
        assert!(error.as_deref().unwrap().contains("save directory"));
    }

    #[test]
    fn form_with_preset_and_save_dir_persists_both() {
        let mut app = test_app();
        update(&mut app, key(KeyCode::Char('a')));
        for c in "Roms".chars() {
            update(&mut app, key(KeyCode::Char(c)));
        }
        for _ in 0..4 {
            update(&mut app, key(KeyCode::Tab));
        }
        update(&mut app, key(KeyCode::Right));
        update(&mut app, key(KeyCode::Tab));
        for c in "/ra/saves".chars() {
            update(&mut app, key(KeyCode::Char(c)));
        }
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(matches!(&cmds[..], [Cmd::SaveConfig]));
        let t = app.targets.last().unwrap();
        assert_eq!(t.preset.as_deref(), Some("retroarch"));
        assert_eq!(
            t.save_dir.as_deref(),
            Some(std::path::Path::new("/ra/saves"))
        );
    }

    fn found(target_id: usize, key: &str) -> ConflictEntry {
        ConflictEntry {
            save_key: key.into(),
            label: format!("T \u{b7} {key}"),
            remote_hash: format!("rh-{key}"),
            ..make_conflict_entry(target_id)
        }
    }

    #[test]
    fn conflict_found_streams_in_and_scan_done_with_empty_inbox_returns_to_dashboard() {
        let mut app = test_app_with_creds();
        let cmds = update(&mut app, key(KeyCode::Char('C')));
        assert!(matches!(&cmds[..], [Cmd::ScanConflicts]));
        assert_eq!(app.screen, Screen::Conflicts);
        assert!(app.conflict_scanning);
        update(&mut app, Msg::ConflictFound(found(0, "a")));
        update(&mut app, Msg::ConflictFound(found(0, "a")));
        update(&mut app, Msg::ConflictFound(found(1, "b")));
        assert_eq!(app.conflicts.len(), 2);
        let cmds = update(&mut app, Msg::ConflictScanDone { error: None });
        assert!(cmds.is_empty() && app.screen == Screen::Conflicts);

        let mut app = test_app_with_creds();
        update(&mut app, key(KeyCode::Char('C')));
        let cmds = update(&mut app, Msg::ConflictScanDone { error: None });
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(matches!(&cmds[..], [Cmd::RefreshAllStatuses]));
    }

    #[test]
    fn scan_error_keeps_inbox_open_with_notice() {
        let mut app = test_app_with_creds();
        update(&mut app, key(KeyCode::Char('C')));
        update(
            &mut app,
            Msg::ConflictScanDone {
                error: Some("boom".into()),
            },
        );
        assert_eq!(app.screen, Screen::Conflicts);
        assert_eq!(app.conflict_notice.as_deref(), Some("boom"));
    }

    #[test]
    fn keep_remote_confirms_then_enqueues_exactly_one_job() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts.push(found(0, "a"));
        assert!(update(&mut app, key(KeyCode::Char('r'))).is_empty());
        let Overlay::Confirm { message, .. } = &app.overlay else {
            panic!("confirm expected");
        };
        assert!(message.contains("Replace your local save") && message.contains("other-dev"));
        assert!(message.contains("safety backup"));
        let cmds = update(&mut app, key(KeyCode::Char('y')));
        assert!(matches!(
            &cmds[..],
            [Cmd::Resolve(ResolveJob::KeepRemote { hash, head_mtime, .. })]
                if hash == "rh-a" && head_mtime == "2026-01-02T00:00:00Z"
        ));
        assert!(app.conflicts[0].queued && app.resolving);
        assert!(update(&mut app, key(KeyCode::Char('r'))).is_empty());
        assert!(
            matches!(app.overlay, Overlay::None),
            "queued entry cannot be re-resolved"
        );
    }

    #[test]
    fn keep_remote_cancel_enqueues_nothing() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts.push(found(0, "a"));
        update(&mut app, key(KeyCode::Char('r')));
        assert!(update(&mut app, key(KeyCode::Esc)).is_empty());
        assert!(app.resolve_queue.is_empty() && !app.resolving);
    }

    #[test]
    fn keep_local_enqueues_only_that_save_and_second_resolve_queues() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts = vec![found(0, "a"), found(0, "b")];
        let cmds = update(&mut app, key(KeyCode::Char('l')));
        assert!(matches!(
            &cmds[..],
            [Cmd::Resolve(ResolveJob::KeepLocal { target_id: 0, group_key })] if group_key == "a"
        ));
        update(&mut app, key(KeyCode::Down));
        assert!(update(&mut app, key(KeyCode::Char('l'))).is_empty());
        assert_eq!(app.resolve_queue.len(), 1);
        assert!(app.conflicts.iter().all(|c| c.queued));
    }

    #[test]
    fn resolve_done_removes_by_group_key_even_after_mid_flight_append_and_starts_next() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts = vec![found(0, "a"), found(0, "b")];
        update(&mut app, key(KeyCode::Char('l')));
        update(&mut app, key(KeyCode::Down));
        update(&mut app, key(KeyCode::Char('l')));
        app.conflicts.insert(0, found(1, "zzz"));
        let cmds = update(
            &mut app,
            Msg::ResolveDone {
                target_id: 0,
                group_key: "a".into(),
                result: Ok(()),
            },
        );
        assert!(app.conflicts.iter().all(|c| c.save_key != "a"));
        assert_eq!(app.conflicts.len(), 2);
        assert!(matches!(
            &cmds[..],
            [Cmd::Resolve(ResolveJob::KeepLocal { group_key, .. })] if group_key == "b"
        ));
    }

    #[test]
    fn resolve_failure_keeps_entry_clears_queued_and_shows_error() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts = vec![found(0, "a")];
        update(&mut app, key(KeyCode::Char('l')));
        let cmds = update(
            &mut app,
            Msg::ResolveDone {
                target_id: 0,
                group_key: "a".into(),
                result: Err("disk full".into()),
            },
        );
        assert!(cmds.is_empty());
        assert!(!app.conflicts[0].queued && !app.resolving);
        assert_eq!(app.conflict_notice.as_deref(), Some("disk full"));
    }

    #[test]
    fn last_resolve_done_empties_inbox_and_returns_to_dashboard() {
        let mut app = test_app_with_creds();
        app.screen = Screen::Conflicts;
        app.conflicts = vec![found(0, "a")];
        update(&mut app, key(KeyCode::Char('l')));
        let cmds = update(
            &mut app,
            Msg::ResolveDone {
                target_id: 0,
                group_key: "a".into(),
                result: Ok(()),
            },
        );
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(matches!(&cmds[..], [Cmd::RefreshAllStatuses]));
    }

    fn settings_app(field: usize) -> App {
        let mut app = test_app_with_creds();
        update(&mut app, key(KeyCode::Char('S')));
        app.settings.as_mut().unwrap().field = field;
        app
    }

    #[test]
    fn settings_password_save_emits_rebuild_creds_and_never_debug_prints_it() {
        let mut app = settings_app(SETTINGS_FIELD_PASSWORD);
        for c in "hunter2".chars() {
            update(&mut app, key(KeyCode::Char(c)));
        }
        assert!(!format!("{:?}", app.settings).contains("hunter2"));
        let cmds = update(&mut app, key(KeyCode::Enter));
        let rebuild = cmds
            .iter()
            .find(|c| matches!(c, Cmd::RebuildCreds { .. }))
            .expect("rebuild cmd");
        assert!(!format!("{rebuild:?}").contains("hunter2"));
        assert!(matches!(rebuild, Cmd::RebuildCreds { password } if password == "hunter2"));
        assert!(app.creds.is_some());
    }

    #[test]
    fn settings_empty_password_leaves_creds_alone() {
        let mut app = settings_app(0);
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(cmds.iter().all(|c| !matches!(c, Cmd::RebuildCreds { .. })));
    }

    #[test]
    fn settings_server_change_clears_creds_and_keychain_session() {
        let mut app = settings_app(0);
        for c in "x".chars() {
            update(&mut app, key(KeyCode::Char(c)));
        }
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(app.creds.is_none());
        assert!(cmds.iter().any(|c| matches!(c, Cmd::ClearSession)));
    }

    #[test]
    fn settings_username_change_without_password_invalidates_session() {
        let mut app = settings_app(3);
        update(&mut app, key(KeyCode::Char('z')));
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(app.creds.is_none());
        assert!(cmds.iter().any(|c| matches!(c, Cmd::ClearSession)));
        assert_eq!(app.username.as_deref(), Some("z"));
    }

    #[test]
    fn target_edits_are_blocked_while_resolves_are_pending() {
        let mut app = test_app_with_creds();
        app.resolving = true;
        app.statuses.insert(0, TargetStatus::Ahead);
        update(&mut app, key(KeyCode::Char('J')));
        assert_eq!(app.selected, 0);
        assert_eq!(app.targets[0].name, "Switch JKSV");
        update(&mut app, key(KeyCode::Char('d')));
        assert!(matches!(app.overlay, Overlay::None));
        assert!(app.log.back().unwrap().text.contains("Wait for"));
        app.resolving = false;
        update(&mut app, key(KeyCode::Char('J')));
        assert_eq!(app.selected, 1);
    }

    #[test]
    fn reopening_the_inbox_while_resolving_keeps_it_instead_of_rescanning() {
        let mut app = test_app_with_creds();
        app.conflicts = vec![found(0, "a")];
        app.resolving = true;
        assert!(update(&mut app, key(KeyCode::Char('C'))).is_empty());
        assert_eq!(app.screen, Screen::Conflicts);
        assert_eq!(app.conflicts.len(), 1);
        assert!(!app.conflict_scanning);
    }

    #[test]
    fn editing_a_target_drops_its_rows_status_and_conflicts() {
        let mut app = test_app();
        app.rows = vec![row(0, "a", false), row(1, "b", false)];
        app.statuses.insert(0, TargetStatus::Ahead);
        app.conflicts = vec![make_conflict_entry(0), make_conflict_entry(1)];
        update(&mut app, key(KeyCode::Char('e')));
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(matches!(&cmds[..], [Cmd::SaveConfig]));
        assert!(app.rows_for(0).is_empty() && app.rows_for(1).len() == 1);
        assert!(!app.statuses.contains_key(&0));
        assert!(app.conflicts.iter().all(|c| c.target_id == 1));
    }

    #[test]
    fn logout_confirms_then_clears_creds_and_session() {
        let mut app = settings_app(SETTINGS_FIELD_LOGOUT);
        assert!(update(&mut app, key(KeyCode::Enter)).is_empty());
        assert!(matches!(app.overlay, Overlay::Confirm { .. }));
        let cmds = update(&mut app, key(KeyCode::Char('y')));
        assert!(app.creds.is_none());
        assert!(matches!(&cmds[..], [Cmd::ClearSession]));
        assert!(app.log.back().unwrap().text.contains("Logged out."));
    }

    #[test]
    fn update_check_flow() {
        let mut app = settings_app(SETTINGS_FIELD_UPDATE);
        let cmds = update(&mut app, key(KeyCode::Enter));
        assert!(matches!(&cmds[..], [Cmd::CheckUpdate]));
        update(
            &mut app,
            Msg::UpdateChecked(Ok(CheckResult::UpToDate("0.5.4".into()))),
        );
        assert_eq!(app.update_status.as_deref(), Some("Up to date (v0.5.4)"));
        let rel = Release {
            version: "9.9.9".into(),
            asset_url: "u".into(),
        };
        update(
            &mut app,
            Msg::UpdateChecked(Ok(CheckResult::Available(rel))),
        );
        let Overlay::Confirm { message, .. } = &app.overlay else {
            panic!("confirm expected");
        };
        assert_eq!(message, "Install v9.9.9?");
        let cmds = update(&mut app, key(KeyCode::Char('y')));
        assert!(matches!(&cmds[..], [Cmd::InstallUpdate(r)] if r.version == "9.9.9"));
        update(&mut app, Msg::UpdateInstalled(Ok("9.9.9".into())));
        assert!(
            app.update_status
                .as_deref()
                .unwrap()
                .starts_with("Updated to v9.9.9")
        );
        update(&mut app, Msg::UpdateInstalled(Err(UpdateError::Install)));
        assert_eq!(
            app.update_status.as_deref(),
            Some(UpdateError::Install.install_message())
        );
    }

    #[test]
    fn offline_message_sets_banner_releases_sync_busy_and_retry_suffix_appears_on_second_failure() {
        let mut app = test_app_with_creds();
        let cmds = update(&mut app, key(KeyCode::Char('A')));
        assert!(matches!(&cmds[..], [Cmd::StartSync { .. }]));
        assert_eq!(app.busy, Some(SYNC_BUSY));
        update(&mut app, Msg::Offline(Some(NoInternetReason::NoNetwork)));
        assert!(app.busy.is_none());
        let banner = app.offline_banner().unwrap();
        assert!(banner.starts_with("No internet connection detected."));
        assert!(banner.contains("(r to retry)") && !banner.contains(RETRY_SUFFIX));
        update(&mut app, key(KeyCode::Char('r')));
        update(&mut app, Msg::Offline(Some(NoInternetReason::NoNetwork)));
        assert!(app.offline_banner().unwrap().ends_with(RETRY_SUFFIX));
        update(&mut app, Msg::Offline(None));
        assert!(app.offline_banner().is_none());
    }

    #[test]
    fn offline_resets_setup_busy_flag() {
        let mut app = test_app();
        app.setup = Some(SetupForm::default());
        app.setup.as_mut().unwrap().busy = true;
        update(
            &mut app,
            Msg::Offline(Some(NoInternetReason::ServerUnreachable)),
        );
        let form = app.setup.as_ref().unwrap();
        assert!(!form.busy);
        assert_eq!(
            form.error.as_deref(),
            Some("Can't reach your Waystone server.")
        );
    }

    #[test]
    fn push_log_writes_ui_tagged_line_without_a_logger() {
        let mut app = test_app();
        app.push_log("hello".into());
        assert_eq!(app.log.back().unwrap().text, "hello");
    }
}
