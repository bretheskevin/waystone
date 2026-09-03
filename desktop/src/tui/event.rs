use crate::tui::app::{App, Msg};
use crossterm::event::KeyEvent;

pub fn map_key(key: KeyEvent, _app: &App) -> Option<Msg> {
    Some(Msg::Key(key))
}
