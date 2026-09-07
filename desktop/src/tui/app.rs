use crate::config::{SyncTarget, WaystoneConfig};
use crate::webdav::{BlockingWebDav, WebDavClient};
use crossterm::event::KeyEvent;
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use waystone_core::crypto::Vault;
use zeroize::Zeroize;

const MAX_LOG_LINES: usize = 200;

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
    SpawnPush(usize),
    SpawnPull(usize),
    RefreshStatus(usize),
    RefreshAllStatuses,
    AttemptUnlock {
        passphrase: String,
        webdav_password: Option<String>,
        recovery: bool,
    },
    SaveConfig,
    Quit,
    ResolveKeepLocal {
        target_id: usize,
        save_key: String,
    },
    ResolveKeepRemote {
        target_id: usize,
        save_key: String,
        remote_hash: String,
    },
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
            Self::SpawnPush(id) => f.debug_tuple("SpawnPush").field(id).finish(),
            Self::SpawnPull(id) => f.debug_tuple("SpawnPull").field(id).finish(),
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
            Self::ResolveKeepLocal {
                target_id,
                save_key,
            } => f
                .debug_struct("ResolveKeepLocal")
                .field("target_id", target_id)
                .field("save_key", save_key)
                .finish(),
            Self::ResolveKeepRemote {
                target_id,
                save_key,
                ..
            } => f
                .debug_struct("ResolveKeepRemote")
                .field("target_id", target_id)
                .field("save_key", save_key)
                .finish(),
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
    Conflicts {
        target_id: usize,
        entries: Vec<ConflictEntry>,
    },
    History {
        entries: Vec<HistoryView>,
    },
    Snapshots {
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

#[derive(Debug, Clone)]
pub struct SettingsForm {
    pub server_url: String,
    pub conflict_policy: waystone_core::conflict::ConflictPolicy,
    pub username: String,
    pub safety_backup: bool,
    pub field: usize,
    pub error: Option<String>,
}

pub struct SetupOk {
    pub recovery_key: String,
    pub vault: Arc<Vault>,
    pub dav: Arc<WebDavClient>,
    pub blocking_dav: Arc<BlockingWebDav>,
}

#[derive(Debug, Clone)]
pub enum PendingCmd {
    Push(usize),
    Pull(usize),
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
    },
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
        }
    }

    pub fn push_log(&mut self, text: String) {
        self.log.push_back(LogLine { text });
        while self.log.len() > MAX_LOG_LINES {
            self.log.pop_front();
        }
    }

    pub fn needs_webdav_password(&self) -> bool {
        std::env::var("WAYSTONE_WEBDAV_PASSWORD").is_err()
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
                    if app.screen == Screen::Conflicts {
                        app.conflicts.retain(|c| c.target_id != target_id);
                        if app.conflict_sel >= app.conflicts.len() && !app.conflicts.is_empty() {
                            app.conflict_sel = app.conflicts.len() - 1;
                        }
                        if app.conflicts.is_empty() {
                            app.screen = Screen::Dashboard;
                            app.conflict_sel = 0;
                        }
                        return vec![Cmd::RefreshStatus(target_id)];
                    }
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

        Msg::Conflicts { target_id, entries } => {
            app.conflicts.retain(|c| c.target_id != target_id);
            app.conflicts.extend(entries);
            if app.conflict_sel >= app.conflicts.len() && !app.conflicts.is_empty() {
                app.conflict_sel = app.conflicts.len() - 1;
            }
            if app.conflicts.is_empty() {
                app.conflict_sel = 0;
            }
            vec![]
        }

        Msg::History { entries } => {
            app.history_entries = entries;
            app.history_selected = 0;
            app.screen = Screen::History;
            vec![]
        }

        Msg::Snapshots { entries } => {
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
            pending
                .map(|p| vec![pending_to_cmd(p)])
                .unwrap_or_else(|| vec![Cmd::RefreshAllStatuses])
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

fn pending_to_cmd(p: PendingCmd) -> Cmd {
    match p {
        PendingCmd::Push(id) => Cmd::SpawnPush(id),
        PendingCmd::Pull(id) => Cmd::SpawnPull(id),
        PendingCmd::RefreshStatus(id) => Cmd::RefreshStatus(id),
        PendingCmd::RefreshAllStatuses => Cmd::RefreshAllStatuses,
        // DeleteTarget is handled directly in Confirm overlay; not reachable from unlock flow
        PendingCmd::DeleteTarget(_) => Cmd::Quit,
        PendingCmd::ResolveKeepLocal {
            target_id,
            save_key,
        } => Cmd::ResolveKeepLocal {
            target_id,
            save_key,
        },
        PendingCmd::ResolveKeepRemote {
            target_id,
            save_key,
            remote_hash,
        } => Cmd::ResolveKeepRemote {
            target_id,
            save_key,
            remote_hash,
        },
        PendingCmd::LoadHistory { target_id } => Cmd::LoadHistory { target_id },
    }
}

fn handle_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
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

fn handle_dashboard_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if app.busy.is_some() {
        if key.code == KeyCode::Char('q') {
            return vec![Cmd::Quit];
        }
        return vec![];
    }

    match key.code {
        KeyCode::Char('q') => vec![Cmd::Quit],
        KeyCode::Char('?') => {
            app.overlay = Overlay::Help;
            vec![]
        }
        KeyCode::Down | KeyCode::Char('j') => {
            if !app.targets.is_empty() {
                app.selected = (app.selected + 1).min(app.targets.len() - 1);
            }
            vec![]
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.selected > 0 {
                app.selected -= 1;
            }
            vec![]
        }
        KeyCode::Char('J') => {
            if app.selected + 1 < app.targets.len() {
                app.targets.swap(app.selected, app.selected + 1);
                app.selected += 1;
                app.dirty = true;
            }
            vec![]
        }
        KeyCode::Char('K') => {
            if app.selected > 0 {
                app.targets.swap(app.selected, app.selected - 1);
                app.selected -= 1;
                app.dirty = true;
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
        KeyCode::Char('p') => {
            if app.targets.is_empty() {
                return vec![];
            }
            ensure_creds_then(app, PendingCmd::Push(app.selected))
        }
        KeyCode::Char('P') => {
            if app.targets.is_empty() {
                return vec![];
            }
            ensure_creds_then(app, PendingCmd::Pull(app.selected))
        }
        KeyCode::Char('r') => {
            if app.targets.is_empty() {
                return vec![];
            }
            ensure_creds_then(app, PendingCmd::RefreshStatus(app.selected))
        }
        KeyCode::Char('C') => {
            if app.conflicts.is_empty() {
                app.push_log("No conflicts to resolve.".into());
                return vec![];
            }
            app.screen = Screen::Conflicts;
            app.conflict_sel = 0;
            vec![]
        }
        KeyCode::Char('S') => {
            app.settings = Some(SettingsForm {
                server_url: app.server_url.clone(),
                conflict_policy: app.conflict_policy,
                username: app.username.clone().unwrap_or_default(),
                safety_backup: app.safety_backup,
                field: 0,
                error: None,
            });
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
        _ => vec![],
    }
}

fn handle_conflicts_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if app.busy.is_some() {
        if key.code == KeyCode::Char('q') {
            return vec![Cmd::Quit];
        }
        return vec![];
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
        KeyCode::Char('l') => {
            if let Some(entry) = app.conflicts.get(app.conflict_sel) {
                ensure_creds_then(
                    app,
                    PendingCmd::ResolveKeepLocal {
                        target_id: entry.target_id,
                        save_key: entry.save_key.clone(),
                    },
                )
            } else {
                vec![]
            }
        }
        KeyCode::Char('r') => {
            if let Some(entry) = app.conflicts.get(app.conflict_sel) {
                ensure_creds_then(
                    app,
                    PendingCmd::ResolveKeepRemote {
                        target_id: entry.target_id,
                        save_key: entry.save_key.clone(),
                        remote_hash: entry.remote_hash.clone(),
                    },
                )
            } else {
                vec![]
            }
        }
        _ => vec![],
    }
}

fn handle_history_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    if app.busy.is_some() {
        if key.code == KeyCode::Char('q') {
            return vec![Cmd::Quit];
        }
        return vec![];
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

    if app.busy.is_some() {
        if key.code == KeyCode::Char('q') {
            return vec![Cmd::Quit];
        }
        return vec![];
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
        vec![pending_to_cmd(pending)]
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
                std::env::var("WAYSTONE_WEBDAV_PASSWORD").ok()
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
        KeyCode::Backspace => {
            let field = if *field_index == 0 {
                passphrase
            } else {
                webdav_password
            };
            field.pop();
            vec![]
        }
        KeyCode::Char(c) => {
            let field = if *field_index == 0 {
                passphrase
            } else {
                webdav_password
            };
            field.push(c);
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
        ref mut field_index,
        ref mut error,
    } = app.overlay
    else {
        return vec![];
    };
    let mode = mode.clone();

    match key.code {
        KeyCode::Esc => {
            app.overlay = Overlay::None;
            vec![]
        }
        KeyCode::Tab | KeyCode::Down => {
            *field_index = (*field_index + 1).min(3);
            vec![]
        }
        KeyCode::BackTab | KeyCode::Up => {
            *field_index = field_index.saturating_sub(1);
            vec![]
        }
        KeyCode::Enter => {
            let target = SyncTarget {
                name: name.clone(),
                path: path.clone().into(),
                adapter: adapter.clone(),
                system: system.clone(),
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
                            }
                        }
                    }
                    app.dirty = true;
                    app.overlay = Overlay::None;
                    vec![Cmd::SaveConfig]
                }
            }
        }
        KeyCode::Backspace => {
            let field = match field_index {
                0 => name,
                1 => path,
                2 => adapter,
                _ => system,
            };
            field.pop();
            vec![]
        }
        KeyCode::Char(c) => {
            let field = match field_index {
                0 => name,
                1 => path,
                2 => adapter,
                _ => system,
            };
            field.push(c);
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
                        if app.selected >= app.targets.len() && app.selected > 0 {
                            app.selected -= 1;
                        }
                        app.dirty = true;
                    }
                    vec![Cmd::SaveConfig]
                }
                other => vec![pending_to_cmd(other)],
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
            if form.server_url.trim().is_empty() {
                form.error = Some("Server URL is required.".into());
                return vec![];
            }
            if !form.server_url.starts_with("http://") && !form.server_url.starts_with("https://") {
                form.error = Some("Server URL must start with http:// or https://".into());
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
        KeyCode::Backspace => {
            let field = match form.field {
                0 => &mut form.server_url,
                1 => &mut form.username,
                2 => &mut form.password,
                3 => &mut form.passphrase,
                _ => &mut form.confirm,
            };
            field.pop();
            vec![]
        }
        KeyCode::Char(c) => {
            let field = match form.field {
                0 => &mut form.server_url,
                1 => &mut form.username,
                2 => &mut form.password,
                3 => &mut form.passphrase,
                _ => &mut form.confirm,
            };
            field.push(c);
            vec![]
        }
        _ => vec![],
    }
}

fn submit_settings(app: &mut App) -> Vec<Cmd> {
    let Some(ref form) = app.settings else {
        return vec![];
    };
    if form.server_url.trim().is_empty() {
        if let Some(ref mut f) = app.settings {
            f.error = Some("Server URL is required.".into());
        }
        return vec![];
    }
    if !form.server_url.starts_with("http://") && !form.server_url.starts_with("https://") {
        if let Some(ref mut f) = app.settings {
            f.error = Some("Server URL must start with http:// or https://".into());
        }
        return vec![];
    }
    let server_changed = form.server_url != app.server_url;
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
    if server_changed {
        app.creds = None;
        app.statuses.clear();
        app.push_log("Server URL changed -- session invalidated.".into());
    }
    app.settings = None;
    app.screen = Screen::Dashboard;
    app.push_log("Settings saved.".into());
    vec![Cmd::SaveSettings(cfg)]
}

fn handle_settings_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Some(ref mut form) = app.settings else {
        return vec![];
    };
    // Fields: 0=server_url, 1=conflict_policy, 2=safety_backup, 3=username
    // Device ID is read-only and not a field index
    let max_field: usize = 3;

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
        KeyCode::Enter => submit_settings(app),
        KeyCode::Backspace => {
            match form.field {
                0 => {
                    form.server_url.pop();
                }
                1 | 2 => {}
                _ => {
                    form.username.pop();
                }
            }
            vec![]
        }
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
        KeyCode::Char(c) => {
            match form.field {
                0 => form.server_url.push(c),
                1 | 2 => {}
                _ => form.username.push(c),
            }
            vec![]
        }
        _ => vec![],
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
                SyncTarget {
                    name: "Switch JKSV".into(),
                    path: "/saves/jksv".into(),
                    adapter: "jksv".into(),
                    system: "switch".into(),
                },
                SyncTarget {
                    name: "GBA mGBA".into(),
                    path: "/saves/mgba".into(),
                    adapter: "mgba".into(),
                    system: "gba".into(),
                },
                SyncTarget {
                    name: "NDS Twilight".into(),
                    path: "/saves/twilight".into(),
                    adapter: "twilight".into(),
                    system: "nds".into(),
                },
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
    fn screen_switch_to_conflicts_noop_when_empty() {
        let mut app = test_app();
        assert!(app.conflicts.is_empty());
        update(&mut app, key(KeyCode::Char('C')));
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(app.log.back().unwrap().text.contains("No conflicts"));
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
    fn keep_remote_emits_resolve_cmd_with_creds() {
        let mut app = test_app();
        app.screen = Screen::Conflicts;
        let (vault, _) = waystone_core::crypto::Vault::init("test").unwrap();
        let dav = crate::webdav::WebDavClient::new("http://localhost", None, None);
        app.creds = Some(SessionCreds {
            vault: Arc::new(vault),
            dav: Arc::new(dav),
            blocking_dav: Arc::new(BlockingWebDav::new("http://localhost", None, None)),
        });
        let mut entry = make_conflict_entry(0);
        entry.remote_hash = "rh123".into();
        app.conflicts.push(entry);
        let cmds = update(&mut app, key(KeyCode::Char('r')));
        assert!(cmds.iter().any(|c| matches!(
            c,
            Cmd::ResolveKeepRemote { remote_hash, .. } if remote_hash == "rh123"
        )));
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
        };
        update(
            &mut app,
            Msg::Conflicts {
                target_id: 0,
                entries: vec![new_entry],
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

        update(
            &mut app,
            Msg::Conflicts {
                target_id: 0,
                entries: vec![],
            },
        );

        assert!(app.conflicts.is_empty());
    }

    #[test]
    fn action_done_on_conflicts_screen_removes_entry_and_refreshes() {
        let mut app = test_app();
        app.screen = Screen::Conflicts;
        app.busy = Some(0);
        app.conflicts.push(make_conflict_entry(0));

        let cmds = update(
            &mut app,
            Msg::ActionDone {
                target_id: 0,
                result: ActionResult::Ok("resolved".into()),
            },
        );

        assert!(app.conflicts.is_empty());
        assert_eq!(app.screen, Screen::Dashboard);
        assert!(cmds.iter().any(|c| matches!(c, Cmd::RefreshStatus(0))));
    }
    // --- Setup / Settings / first-run tests ---

    fn setup_config() -> WaystoneConfig {
        WaystoneConfig {
            device_id: "dev".into(),
            server_url: String::new(),
            conflict_policy: waystone_core::conflict::ConflictPolicy::NewestWins,
            username: None,
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
        update(
            &mut app,
            Msg::History {
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
        update(&mut app, Msg::History { entries: vec![] });
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
        update(
            &mut app,
            Msg::Snapshots {
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
        update(&mut app, Msg::Snapshots { entries: vec![] });
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
}
