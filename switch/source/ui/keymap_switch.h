#pragma once
#include <borealis.hpp>
#include <cstddef>
#include <string>
#include "keymap.h"

inline brls::ControllerButton ws_to_brls(WsButton b) {
    switch (b) {
    case WsButton::A:      return brls::BUTTON_A;
    case WsButton::B:      return brls::BUTTON_B;
    case WsButton::X:      return brls::BUTTON_X;
    case WsButton::Y:      return brls::BUTTON_Y;
    case WsButton::L:      return brls::BUTTON_LB;
    case WsButton::R:      return brls::BUTTON_RB;
    case WsButton::Start:  return brls::BUTTON_START;
    case WsButton::Select: return brls::BUTTON_BACK;
    }
    return brls::BUTTON_A;
}

inline brls::ControllerButton ws_brls(WsAction a) { return ws_to_brls(ws_button(a)); }

// NintendoExt shared-font PUA codepoints (rendered via FONT_SWITCH_ICONS fallback).
inline const char* ws_glyph(WsButton b) {
    switch (b) {
    case WsButton::A:      return "\xEE\x82\xA0";
    case WsButton::B:      return "\xEE\x82\xA1";
    case WsButton::X:      return "\xEE\x82\xA2";
    case WsButton::Y:      return "\xEE\x82\xA3";
    case WsButton::L:      return "\xEE\x82\xA4";
    case WsButton::R:      return "\xEE\x82\xA5";
    case WsButton::Start:  return "\xEE\x82\xB5";
    case WsButton::Select: return "\xEE\x82\xB6";
    }
    return "?";
}

inline const WsHintStyle& ws_hint_style() {
    static const WsHintStyle style = { &ws_glyph, " ", "   \xc2\xb7   " };
    return style;
}

inline std::string ws_hint(WsAction a, const char* label_override = nullptr) {
    return ws_format_hint(a, ws_hint_style(), label_override);
}

template <size_t N>
inline std::string ws_hint_bar(const WsAction (&acts)[N]) {
    return ws_format_hint_bar(acts, N, ws_hint_style());
}
