use crate::helpers;
use crate::tui::app::{App, FormMode, Overlay, Screen, TargetStatus};
use crate::tui::theme;
use ratatui::Frame;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, List, ListItem, Paragraph, Wrap};

const SPINNER_FRAMES: &[char] = &['|', '/', '-', '\\'];

pub fn ui(frame: &mut Frame, app: &App) {
    match app.screen {
        Screen::Dashboard => render_dashboard(frame, app),
        Screen::Conflicts => render_conflicts_screen(frame, app),
        Screen::History => render_history_screen(frame, app),
        Screen::Snapshots => render_snapshots_screen(frame, app),
        Screen::Setup => render_setup(frame, app),
        Screen::Settings => render_settings(frame, app),
    }

    match &app.overlay {
        Overlay::None => {}
        Overlay::Help => render_help_overlay(frame),
        Overlay::Unlock { .. } => render_unlock_overlay(frame, app),
        Overlay::TargetForm { .. } => render_form_overlay(frame, app),
        Overlay::Confirm { message, .. } => render_confirm_overlay(frame, message),
        Overlay::RecoveryKey { .. } => render_recovery_key_overlay(frame, app),
    }
}

fn render_dashboard(frame: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(5),
            Constraint::Length(8),
        ])
        .split(frame.area());

    render_header(frame, app, chunks[0]);
    render_targets(frame, app, chunks[1]);
    render_log(frame, app, chunks[2]);
}

fn render_conflicts_screen(frame: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(5),
            Constraint::Length(3),
        ])
        .split(frame.area());

    let spinner = if app.busy.is_some() {
        SPINNER_FRAMES[app.spinner as usize % SPINNER_FRAMES.len()]
    } else {
        ' '
    };
    let header_line = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Conflicts",
            Style::default()
                .fg(theme::WARNING)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(format!(" | {} item(s) {} ", app.conflicts.len(), spinner,)),
    ]);
    frame.render_widget(Paragraph::new(header_line), chunks[0]);

    if app.conflicts.is_empty() {
        let empty = Paragraph::new(Line::raw("  No conflicts")).block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(theme::NEUTRAL_700))
                .title(" Conflicts Inbox "),
        );
        frame.render_widget(empty, chunks[1]);
    } else {
        let items: Vec<ListItem> = app
            .conflicts
            .iter()
            .enumerate()
            .map(|(i, entry)| {
                let is_selected = i == app.conflict_sel;
                let detail = if is_selected {
                    vec![
                        Line::from(vec![Span::styled(
                            &entry.label,
                            Style::default().fg(theme::NEUTRAL_50),
                        )]),
                        Line::from(vec![
                            Span::raw("  LOCAL  "),
                            Span::styled(
                                &entry.local.hash[..12.min(entry.local.hash.len())],
                                Style::default().fg(theme::SYNC),
                            ),
                            Span::raw(format!("  {}", entry.local.mtime)),
                        ]),
                        Line::from(vec![
                            Span::raw("  REMOTE "),
                            Span::styled(
                                &entry.remote.hash[..12.min(entry.remote.hash.len())],
                                Style::default().fg(theme::WARNING),
                            ),
                            Span::raw(format!(
                                "  {}  ({})",
                                entry.remote.mtime,
                                entry.remote.device_id.as_deref().unwrap_or("?"),
                            )),
                        ]),
                    ]
                } else {
                    vec![Line::from(vec![Span::styled(
                        &entry.label,
                        Style::default().fg(theme::NEUTRAL_300),
                    )])]
                };
                ListItem::new(detail)
            })
            .collect();

        let list = List::new(items)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .border_style(Style::default().fg(theme::WARNING))
                    .title(" Conflicts Inbox "),
            )
            .highlight_style(
                Style::default()
                    .bg(theme::PRIMARY_800)
                    .add_modifier(Modifier::BOLD),
            )
            .highlight_symbol("> ");

        let mut state = ratatui::widgets::ListState::default();
        state.select(Some(app.conflict_sel));
        frame.render_stateful_widget(list, chunks[1], &mut state);
    }

    let footer = Line::from(vec![
        Span::styled(
            " [\u{2191}\u{2193}] ",
            Style::default().fg(theme::NEUTRAL_50),
        ),
        Span::styled("select", Style::default().fg(theme::NEUTRAL_400)),
        Span::styled(" [l] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("keep local", Style::default().fg(theme::SYNC)),
        Span::styled(" [r] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("keep remote", Style::default().fg(theme::WARNING)),
        Span::styled(" [esc] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("back", Style::default().fg(theme::NEUTRAL_400)),
    ]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

fn render_history_screen(frame: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(5),
            Constraint::Length(3),
        ])
        .split(frame.area());

    let spinner = if app.busy.is_some() {
        SPINNER_FRAMES[app.spinner as usize % SPINNER_FRAMES.len()]
    } else {
        ' '
    };
    let header_line = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "History",
            Style::default()
                .fg(theme::SYNC)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(format!(
            " | {} version(s) {} ",
            app.history_entries.len(),
            spinner,
        )),
    ]);
    frame.render_widget(Paragraph::new(header_line), chunks[0]);

    if app.history_entries.is_empty() {
        let empty = Paragraph::new(Line::raw("  No history yet")).block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(theme::NEUTRAL_700))
                .title(" History "),
        );
        frame.render_widget(empty, chunks[1]);
    } else {
        let items: Vec<ListItem> = app
            .history_entries
            .iter()
            .enumerate()
            .map(|(i, entry)| {
                let is_selected = i == app.history_selected;
                let detail = if is_selected {
                    let hash_short = &entry.hash[..12.min(entry.hash.len())];
                    vec![
                        Line::from(vec![Span::styled(
                            &entry.label,
                            Style::default().fg(theme::NEUTRAL_50),
                        )]),
                        Line::from(vec![
                            Span::raw("  "),
                            Span::styled(hash_short, Style::default().fg(theme::SYNC)),
                            Span::raw(format!(
                                "  {}  {}  mtime={}",
                                entry.timestamp, entry.device_id, entry.mtime
                            )),
                        ]),
                    ]
                } else {
                    vec![Line::from(vec![Span::styled(
                        &entry.label,
                        Style::default().fg(theme::NEUTRAL_300),
                    )])]
                };
                ListItem::new(detail)
            })
            .collect();

        let list = List::new(items)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .border_style(Style::default().fg(theme::SYNC))
                    .title(" History "),
            )
            .highlight_style(
                Style::default()
                    .bg(theme::PRIMARY_800)
                    .add_modifier(Modifier::BOLD),
            )
            .highlight_symbol("> ");

        let mut state = ratatui::widgets::ListState::default();
        state.select(Some(app.history_selected));
        frame.render_stateful_widget(list, chunks[1], &mut state);
    }

    let footer = Line::from(vec![
        Span::styled(
            " [\u{2191}\u{2193}] ",
            Style::default().fg(theme::NEUTRAL_50),
        ),
        Span::styled("select", Style::default().fg(theme::NEUTRAL_400)),
        Span::styled(" [enter] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("restore", Style::default().fg(theme::SYNC)),
        Span::styled(" [esc] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("back", Style::default().fg(theme::NEUTRAL_400)),
    ]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

fn render_snapshots_screen(frame: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(1),
            Constraint::Min(5),
            Constraint::Length(3),
        ])
        .split(frame.area());

    let spinner = if app.busy.is_some() {
        SPINNER_FRAMES[app.spinner as usize % SPINNER_FRAMES.len()]
    } else {
        ' '
    };
    let header_line = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Snapshots",
            Style::default()
                .fg(theme::SUCCESS)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(format!(
            " | {} snapshot(s) {} ",
            app.snapshot_entries.len(),
            spinner,
        )),
    ]);
    frame.render_widget(Paragraph::new(header_line), chunks[0]);

    if app.snapshot_entries.is_empty() {
        let empty = Paragraph::new(Line::raw("  No snapshots yet")).block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(theme::NEUTRAL_700))
                .title(" Snapshots "),
        );
        frame.render_widget(empty, chunks[1]);
    } else {
        let items: Vec<ListItem> = app
            .snapshot_entries
            .iter()
            .enumerate()
            .map(|(i, entry)| {
                let is_selected = i == app.snapshot_selected;
                let detail = if is_selected {
                    vec![
                        Line::from(vec![Span::styled(
                            &entry.label,
                            Style::default().fg(theme::NEUTRAL_50),
                        )]),
                        Line::from(vec![
                            Span::raw("  "),
                            Span::styled(&entry.timestamp, Style::default().fg(theme::SYNC)),
                            Span::raw(format!(
                                "  {} files  {}",
                                entry.file_count,
                                helpers::human_size(entry.total_bytes)
                            )),
                        ]),
                    ]
                } else {
                    vec![Line::from(vec![Span::styled(
                        &entry.label,
                        Style::default().fg(theme::NEUTRAL_300),
                    )])]
                };
                ListItem::new(detail)
            })
            .collect();

        let list = List::new(items)
            .block(
                Block::default()
                    .borders(Borders::ALL)
                    .border_style(Style::default().fg(theme::SUCCESS))
                    .title(" Snapshots "),
            )
            .highlight_style(
                Style::default()
                    .bg(theme::PRIMARY_800)
                    .add_modifier(Modifier::BOLD),
            )
            .highlight_symbol("> ");

        let mut state = ratatui::widgets::ListState::default();
        state.select(Some(app.snapshot_selected));
        frame.render_stateful_widget(list, chunks[1], &mut state);
    }

    let footer = Line::from(vec![
        Span::styled(
            " [\u{2191}\u{2193}] ",
            Style::default().fg(theme::NEUTRAL_50),
        ),
        Span::styled("select", Style::default().fg(theme::NEUTRAL_400)),
        Span::styled(" [enter] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("restore", Style::default().fg(theme::SUCCESS)),
        Span::styled(" [esc] ", Style::default().fg(theme::NEUTRAL_50)),
        Span::styled("back", Style::default().fg(theme::NEUTRAL_400)),
    ]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

fn render_header(frame: &mut Frame, app: &App, area: Rect) {
    let spinner = if app.busy.is_some() {
        SPINNER_FRAMES[app.spinner as usize % SPINNER_FRAMES.len()]
    } else {
        ' '
    };
    let status_text = if app.creds.is_some() {
        "unlocked"
    } else {
        "locked"
    };
    let conflict_hint = if !app.conflicts.is_empty() {
        format!(" | {} conflict(s) [C]", app.conflicts.len())
    } else {
        String::new()
    };
    let line = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(format!(
            "| {} | {} targets | {} ",
            status_text,
            app.targets.len(),
            spinner
        )),
        Span::styled(
            if app.dirty { "[modified]" } else { "" },
            Style::default().fg(theme::WARNING),
        ),
        Span::styled(conflict_hint, Style::default().fg(theme::WARNING)),
    ]);
    frame.render_widget(Paragraph::new(line), area);
}

fn render_targets(frame: &mut Frame, app: &App, area: Rect) {
    let items: Vec<ListItem> = app
        .targets
        .iter()
        .enumerate()
        .map(|(i, t)| {
            let status = app.statuses.get(&i).unwrap_or(&TargetStatus::Unknown);
            let color = theme::status_color(status.label());
            let busy_mark = if app.busy == Some(i) { ">> " } else { "   " };
            let line = Line::from(vec![
                Span::raw(busy_mark),
                Span::styled(&t.name, Style::default().fg(theme::NEUTRAL_50)),
                Span::raw(format!("  [{}:{}]  ", t.adapter, t.system)),
                Span::styled(status.label(), Style::default().fg(color)),
            ]);
            ListItem::new(line)
        })
        .collect();

    let list = List::new(items)
        .block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(theme::NEUTRAL_700))
                .title(" Sync Targets "),
        )
        .highlight_style(
            Style::default()
                .bg(theme::PRIMARY_800)
                .add_modifier(Modifier::BOLD),
        )
        .highlight_symbol("> ");

    let mut state = ratatui::widgets::ListState::default();
    state.select(Some(app.selected));
    frame.render_stateful_widget(list, area, &mut state);
}

fn render_log(frame: &mut Frame, app: &App, area: Rect) {
    let lines: Vec<Line> = app
        .log
        .iter()
        .rev()
        .take(area.height as usize)
        .map(|l| Line::raw(&l.text))
        .collect();
    let paragraph = Paragraph::new(lines)
        .block(
            Block::default()
                .borders(Borders::ALL)
                .border_style(Style::default().fg(theme::NEUTRAL_700))
                .title(" Log "),
        )
        .wrap(Wrap { trim: false });
    frame.render_widget(paragraph, area);
}

fn centered_rect(width: u16, height: u16, area: Rect) -> Rect {
    let x = area.x + area.width.saturating_sub(width) / 2;
    let y = area.y + area.height.saturating_sub(height) / 2;
    Rect::new(x, y, width.min(area.width), height.min(area.height))
}

fn render_help_overlay(frame: &mut Frame) {
    let area = centered_rect(50, 18, frame.area());
    frame.render_widget(Clear, area);
    let text = vec![
        Line::styled(
            "Keybindings",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
        Line::raw("j/k or arrows  Select target"),
        Line::raw("p              Push selected"),
        Line::raw("P              Pull selected"),
        Line::raw("r              Refresh status"),
        Line::raw("C              Conflicts inbox"),
        Line::raw("h              History"),
        Line::raw("b              Snapshots (offline)"),
        Line::raw("S              Settings"),
        Line::raw("a              Add target"),
        Line::raw("e              Edit target"),
        Line::raw("d              Delete target"),
        Line::raw("J/K            Reorder targets"),
        Line::raw("?              This help"),
        Line::raw("q              Quit"),
        Line::raw(""),
        Line::styled(
            "Press any key to close",
            Style::default().fg(theme::NEUTRAL_400),
        ),
    ];
    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::PRIMARY))
        .title(" Help ");
    frame.render_widget(Paragraph::new(text).block(block), area);
}

fn render_unlock_overlay(frame: &mut Frame, app: &App) {
    let Overlay::Unlock {
        ref passphrase,
        ref webdav_password,
        needs_webdav,
        field_index,
        ref error,
        ..
    } = app.overlay
    else {
        return;
    };
    let height = if needs_webdav { 10 } else { 8 };
    let area = centered_rect(50, height, frame.area());
    frame.render_widget(Clear, area);

    let mut lines = vec![
        Line::styled(
            "Unlock Session",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
    ];
    let pass_display: String = if passphrase.is_empty() {
        " ".into()
    } else {
        "\u{2022}".repeat(passphrase.len())
    };
    let pass_style = if field_index == 0 {
        Style::default()
            .fg(theme::NEUTRAL_50)
            .add_modifier(Modifier::UNDERLINED)
    } else {
        Style::default().fg(theme::NEUTRAL_400)
    };
    lines.push(Line::from(vec![
        Span::raw("Passphrase: "),
        Span::styled(pass_display, pass_style),
    ]));

    if needs_webdav {
        let wdav_display: String = if webdav_password.is_empty() {
            " ".into()
        } else {
            "\u{2022}".repeat(webdav_password.len())
        };
        let wdav_style = if field_index == 1 {
            Style::default()
                .fg(theme::NEUTRAL_50)
                .add_modifier(Modifier::UNDERLINED)
        } else {
            Style::default().fg(theme::NEUTRAL_400)
        };
        lines.push(Line::from(vec![
            Span::raw("WebDAV password: "),
            Span::styled(wdav_display, wdav_style),
        ]));
    }

    if let Some(err) = error {
        lines.push(Line::raw(""));
        lines.push(Line::styled(
            err.as_str(),
            Style::default().fg(theme::ERROR),
        ));
    }
    lines.push(Line::raw(""));
    lines.push(Line::styled(
        "Enter=submit  Esc=cancel  Tab=next field",
        Style::default().fg(theme::NEUTRAL_500),
    ));

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::PRIMARY))
        .title(" Unlock ");
    frame.render_widget(Paragraph::new(lines).block(block), area);
}

fn render_form_overlay(frame: &mut Frame, app: &App) {
    let Overlay::TargetForm {
        ref mode,
        ref name,
        ref path,
        ref adapter,
        ref system,
        field_index,
        ref error,
    } = app.overlay
    else {
        return;
    };
    let title = match mode {
        FormMode::Add => " Add Target ",
        FormMode::Edit(_) => " Edit Target ",
    };
    let area = centered_rect(55, 12, frame.area());
    frame.render_widget(Clear, area);

    let fields = [
        ("Name", name.as_str()),
        ("Path", path.as_str()),
        ("Adapter", adapter.as_str()),
        ("System", system.as_str()),
    ];
    let mut lines = vec![
        Line::styled(
            title.trim(),
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
    ];
    for (i, (label, value)) in fields.iter().enumerate() {
        let style = if field_index == i {
            Style::default()
                .fg(theme::NEUTRAL_50)
                .add_modifier(Modifier::UNDERLINED)
        } else {
            Style::default().fg(theme::NEUTRAL_400)
        };
        lines.push(Line::from(vec![
            Span::raw(format!("{}: ", label)),
            Span::styled(if value.is_empty() { " " } else { value }, style),
        ]));
    }
    if let Some(err) = error {
        lines.push(Line::raw(""));
        lines.push(Line::styled(
            err.as_str(),
            Style::default().fg(theme::ERROR),
        ));
    }
    lines.push(Line::raw(""));
    lines.push(Line::styled(
        "Enter=submit  Esc=cancel  Tab=next field",
        Style::default().fg(theme::NEUTRAL_500),
    ));

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::PRIMARY))
        .title(title);
    frame.render_widget(Paragraph::new(lines).block(block), area);
}

fn render_confirm_overlay(frame: &mut Frame, message: &str) {
    let area = centered_rect(45, 6, frame.area());
    frame.render_widget(Clear, area);
    let lines = vec![
        Line::raw(message),
        Line::raw(""),
        Line::styled(
            "y/Enter = yes   n/Esc = no",
            Style::default().fg(theme::NEUTRAL_400),
        ),
    ];
    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::WARNING))
        .title(" Confirm ");
    frame.render_widget(Paragraph::new(lines).block(block), area);
}

fn render_setup(frame: &mut Frame, app: &App) {
    let Some(ref form) = app.setup else {
        return;
    };
    let area = frame.area();
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Min(10),
            Constraint::Length(2),
        ])
        .split(area);

    let header = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Setup",
            Style::default()
                .fg(theme::SYNC)
                .add_modifier(Modifier::BOLD),
        ),
        Span::raw(" | First-run vault initialization"),
    ]);
    frame.render_widget(Paragraph::new(header), chunks[0]);

    let fields = [
        ("Server URL", form.server_url.as_str(), false),
        ("Username", form.username.as_str(), false),
        ("WebDAV Password", form.password.as_str(), true),
        ("Passphrase", form.passphrase.as_str(), true),
        ("Confirm Passphrase", form.confirm.as_str(), true),
    ];

    let mut lines = vec![
        Line::styled(
            "Initialize Vault",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
    ];
    for (i, (label, value, masked)) in fields.iter().enumerate() {
        let display: String = if *masked {
            if value.is_empty() {
                " ".into()
            } else {
                "\u{2022}".repeat(value.len())
            }
        } else if value.is_empty() {
            " ".into()
        } else {
            (*value).to_string()
        };
        let style = if form.field == i {
            Style::default()
                .fg(theme::NEUTRAL_50)
                .add_modifier(Modifier::UNDERLINED)
        } else {
            Style::default().fg(theme::NEUTRAL_400)
        };
        lines.push(Line::from(vec![
            Span::raw(format!("{}: ", label)),
            Span::styled(display, style),
        ]));
    }

    if form.busy {
        lines.push(Line::raw(""));
        lines.push(Line::styled(
            "Initializing vault...",
            Style::default().fg(theme::SYNC),
        ));
    }

    if let Some(ref err) = form.error {
        lines.push(Line::raw(""));
        lines.push(Line::styled(
            err.as_str(),
            Style::default().fg(theme::ERROR),
        ));
    }

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::PRIMARY))
        .title(" Setup ");
    frame.render_widget(Paragraph::new(lines).block(block), chunks[1]);

    let footer = Line::from(vec![Span::styled(
        "Enter=submit  Esc=quit  Tab=next field",
        Style::default().fg(theme::NEUTRAL_500),
    )]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

fn render_settings(frame: &mut Frame, app: &App) {
    let Some(ref form) = app.settings else {
        return;
    };
    let area = frame.area();
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Min(10),
            Constraint::Length(2),
        ])
        .split(area);

    let header = Line::from(vec![
        Span::styled(
            " waystone ",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Span::styled(
            "Settings",
            Style::default()
                .fg(theme::SYNC)
                .add_modifier(Modifier::BOLD),
        ),
    ]);
    frame.render_widget(Paragraph::new(header), chunks[0]);

    let policy_label = match form.conflict_policy {
        waystone_core::conflict::ConflictPolicy::NewestWins => "newest-wins",
        waystone_core::conflict::ConflictPolicy::Prompt => "prompt",
    };

    let safety_label = if form.safety_backup { "ON" } else { "OFF" };

    let fields: Vec<(&str, String, bool)> = vec![
        ("Server URL", form.server_url.clone(), false),
        (
            "Conflict Policy",
            format!("< {} > (space to toggle)", policy_label),
            false,
        ),
        (
            "Safety Backup",
            format!("< {} > (space to toggle)", safety_label),
            false,
        ),
        ("Username", form.username.clone(), false),
        ("Device ID", app.device_id.clone(), true),
    ];

    let mut lines = vec![
        Line::styled(
            "Settings",
            Style::default()
                .fg(theme::PRIMARY)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
    ];

    for (i, (label, value, read_only)) in fields.iter().enumerate() {
        let style = if *read_only {
            Style::default().fg(theme::NEUTRAL_500)
        } else if form.field == i {
            Style::default()
                .fg(theme::NEUTRAL_50)
                .add_modifier(Modifier::UNDERLINED)
        } else {
            Style::default().fg(theme::NEUTRAL_400)
        };
        let prefix = if *read_only { "(read-only) " } else { "" };
        lines.push(Line::from(vec![
            Span::raw(format!("{}{}: ", prefix, label)),
            Span::styled(
                if value.is_empty() {
                    " "
                } else {
                    value.as_str()
                },
                style,
            ),
        ]));
    }

    if let Some(ref err) = form.error {
        lines.push(Line::raw(""));
        lines.push(Line::styled(
            err.as_str(),
            Style::default().fg(theme::ERROR),
        ));
    }

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::PRIMARY))
        .title(" Settings ");
    frame.render_widget(Paragraph::new(lines).block(block), chunks[1]);

    let footer = Line::from(vec![Span::styled(
        "Enter=save  Esc=discard  Tab=next field  Space=toggle (on policy/safety)",
        Style::default().fg(theme::NEUTRAL_500),
    )]);
    frame.render_widget(Paragraph::new(footer), chunks[2]);
}

fn render_recovery_key_overlay(frame: &mut Frame, app: &App) {
    let Overlay::RecoveryKey { ref key } = app.overlay else {
        return;
    };
    let area = centered_rect(60, 12, frame.area());
    frame.render_widget(Clear, area);

    let lines = vec![
        Line::styled(
            "Recovery Key",
            Style::default()
                .fg(theme::WARNING)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
        Line::styled(
            "Save this key somewhere safe. It CANNOT be shown again.",
            Style::default().fg(theme::ERROR),
        ),
        Line::raw(""),
        Line::styled(
            key.as_str(),
            Style::default()
                .fg(theme::NEUTRAL_50)
                .add_modifier(Modifier::BOLD),
        ),
        Line::raw(""),
        Line::raw(""),
        Line::styled(
            "[s] Save to file   [Enter] I saved it",
            Style::default().fg(theme::NEUTRAL_400),
        ),
    ];

    let block = Block::default()
        .borders(Borders::ALL)
        .border_style(Style::default().fg(theme::WARNING))
        .title(" Recovery Key ");
    frame.render_widget(Paragraph::new(lines).block(block), area);
}
