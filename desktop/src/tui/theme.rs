#![allow(dead_code)]
use ratatui::style::Color;

pub const PRIMARY: Color = Color::Rgb(0x63, 0x66, 0xF1);
pub const SUCCESS: Color = Color::Rgb(0x22, 0xC5, 0x5E);
pub const WARNING: Color = Color::Rgb(0xF5, 0x9E, 0x0B);
pub const ERROR: Color = Color::Rgb(0xEF, 0x44, 0x44);
pub const SYNC: Color = Color::Rgb(0x06, 0xB6, 0xD4);

pub const NEUTRAL_50: Color = Color::Rgb(0xFA, 0xFA, 0xFA);
pub const NEUTRAL_100: Color = Color::Rgb(0xF4, 0xF4, 0xF5);
pub const NEUTRAL_200: Color = Color::Rgb(0xE4, 0xE4, 0xE7);
pub const NEUTRAL_300: Color = Color::Rgb(0xD4, 0xD4, 0xD8);
pub const NEUTRAL_400: Color = Color::Rgb(0xA1, 0xA1, 0xAA);
pub const NEUTRAL_500: Color = Color::Rgb(0x71, 0x71, 0x7A);
pub const NEUTRAL_600: Color = Color::Rgb(0x52, 0x52, 0x5B);
pub const NEUTRAL_700: Color = Color::Rgb(0x3F, 0x3F, 0x46);
pub const NEUTRAL_800: Color = Color::Rgb(0x27, 0x27, 0x2A);
pub const NEUTRAL_900: Color = Color::Rgb(0x18, 0x18, 0x1B);

pub const PRIMARY_400: Color = Color::Rgb(0x81, 0x8C, 0xF8);
pub const PRIMARY_600: Color = Color::Rgb(0x4F, 0x46, 0xE5);
pub const PRIMARY_800: Color = Color::Rgb(0x37, 0x30, 0xA3);

pub fn status_color(status: &str) -> Color {
    match status {
        "in_sync" => SUCCESS,
        "ahead" | "behind" => SYNC,
        "conflict" => WARNING,
        "error" => ERROR,
        _ => NEUTRAL_400,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex_to_rgb(hex: &str) -> Color {
        let hex = hex.trim_start_matches('#');
        let r = u8::from_str_radix(&hex[0..2], 16).unwrap();
        let g = u8::from_str_radix(&hex[2..4], 16).unwrap();
        let b = u8::from_str_radix(&hex[4..6], 16).unwrap();
        Color::Rgb(r, g, b)
    }

    #[test]
    fn theme_constants_match_design_tokens() {
        assert_eq!(PRIMARY, hex_to_rgb("#6366F1"));
        assert_eq!(SUCCESS, hex_to_rgb("#22C55E"));
        assert_eq!(WARNING, hex_to_rgb("#F59E0B"));
        assert_eq!(ERROR, hex_to_rgb("#EF4444"));
        assert_eq!(SYNC, hex_to_rgb("#06B6D4"));
    }

    #[test]
    fn neutral_scale_matches_design_tokens() {
        assert_eq!(NEUTRAL_50, hex_to_rgb("#FAFAFA"));
        assert_eq!(NEUTRAL_100, hex_to_rgb("#F4F4F5"));
        assert_eq!(NEUTRAL_200, hex_to_rgb("#E4E4E7"));
        assert_eq!(NEUTRAL_300, hex_to_rgb("#D4D4D8"));
        assert_eq!(NEUTRAL_400, hex_to_rgb("#A1A1AA"));
        assert_eq!(NEUTRAL_500, hex_to_rgb("#71717A"));
        assert_eq!(NEUTRAL_600, hex_to_rgb("#52525B"));
        assert_eq!(NEUTRAL_700, hex_to_rgb("#3F3F46"));
        assert_eq!(NEUTRAL_800, hex_to_rgb("#27272A"));
        assert_eq!(NEUTRAL_900, hex_to_rgb("#18181B"));
    }

    #[test]
    fn status_color_maps_correctly() {
        assert_eq!(status_color("in_sync"), SUCCESS);
        assert_eq!(status_color("ahead"), SYNC);
        assert_eq!(status_color("behind"), SYNC);
        assert_eq!(status_color("conflict"), WARNING);
        assert_eq!(status_color("error"), ERROR);
        assert_eq!(status_color("unknown"), NEUTRAL_400);
    }
}
