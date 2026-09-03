use crate::tui::app::{App, FormMode, Overlay, TargetStatus};
use crate::tui::theme;
use ratatui::Frame;
use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::{Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Clear, List, ListItem, Paragraph, Wrap};

const SPINNER_FRAMES: &[char] = &['|', '/', '-', '\\'];

pub fn ui(frame: &mut Frame, app: &App) {
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

    match &app.overlay {
        Overlay::None => {}
        Overlay::Help => render_help_overlay(frame),
        Overlay::Unlock { .. } => render_unlock_overlay(frame, app),
        Overlay::TargetForm { .. } => render_form_overlay(frame, app),
        Overlay::Confirm { message, .. } => render_confirm_overlay(frame, message),
    }
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
    let area = centered_rect(50, 14, frame.area());
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
