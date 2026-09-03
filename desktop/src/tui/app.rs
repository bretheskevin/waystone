use crate::config::{SyncTarget, WaystoneConfig};
use crate::webdav::WebDavClient;
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
}

pub enum Cmd {
    SpawnPush(usize),
    SpawnPull(usize),
    RefreshStatus(usize),
    RefreshAllStatuses,
    AttemptUnlock {
        passphrase: String,
        webdav_password: Option<String>,
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
}

impl std::fmt::Debug for Cmd {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::SpawnPush(id) => f.debug_tuple("SpawnPush").field(id).finish(),
            Self::SpawnPull(id) => f.debug_tuple("SpawnPull").field(id).finish(),
            Self::RefreshStatus(id) => f.debug_tuple("RefreshStatus").field(id).finish(),
            Self::RefreshAllStatuses => write!(f, "RefreshAllStatuses"),
            Self::AttemptUnlock { .. } => f
                .debug_struct("AttemptUnlock")
                .field("passphrase", &"[REDACTED]")
                .field("webdav_password", &"[REDACTED]")
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
    UnlockOk {
        vault: Arc<Vault>,
        dav: Arc<WebDavClient>,
    },
    UnlockFailed(String),
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
        passphrase: String,
        webdav_password: String,
        needs_webdav: bool,
        field_index: usize,
        error: Option<String>,
        then: Option<PendingCmd>,
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
                ..
            } => f
                .debug_struct("Unlock")
                .field("passphrase", &"[REDACTED]")
                .field("webdav_password", &"[REDACTED]")
                .field("needs_webdav", needs_webdav)
                .field("field_index", field_index)
                .field("error", error)
                .field("then", then)
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
        }
    }
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
}

impl App {
    pub fn new(config: &WaystoneConfig) -> Self {
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
            screen: Screen::Dashboard,
            conflicts: Vec::new(),
            conflict_sel: 0,
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

    pub fn to_config(&self, base: &WaystoneConfig) -> WaystoneConfig {
        WaystoneConfig {
            targets: self.targets.clone(),
            ..base.clone()
        }
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

        Msg::UnlockOk { vault, dav } => {
            let pending = match &app.overlay {
                Overlay::Unlock { then, .. } => then.clone(),
                _ => Option::None,
            };
            app.creds = Some(SessionCreds { vault, dav });
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
    }
}

fn handle_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    match &app.overlay {
        Overlay::None => match app.screen {
            Screen::Dashboard => handle_dashboard_key(app, key),
            Screen::Conflicts => handle_conflicts_key(app, key),
        },
        Overlay::Unlock { .. } => handle_unlock_key(app, key),
        Overlay::TargetForm { .. } => handle_form_key(app, key),
        Overlay::Confirm { .. } => handle_confirm_key(app, key),
        Overlay::Help => {
            app.overlay = Overlay::None;
            vec![]
        }
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
        };
        vec![]
    }
}

fn handle_unlock_key(app: &mut App, key: KeyEvent) -> Vec<Cmd> {
    use crossterm::event::KeyCode;

    let Overlay::Unlock {
        ref mut passphrase,
        ref mut webdav_password,
        needs_webdav,
        ref mut field_index,
        ref mut error,
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
                *error = Some("Passphrase is required.".into());
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
            }]
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
        };
        App::new(&config)
    }

    fn make_conflict_entry(target_id: usize) -> ConflictEntry {
        ConflictEntry {
            target_id,
            label: format!("Switch \u{00b7} Test/main"),
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
}
