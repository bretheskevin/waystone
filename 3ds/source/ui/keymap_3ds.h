#pragma once
#include <3ds.h>
#include <cstddef>
#include <string>
#include "keymap.h"

inline u32 ws_to_key(WsButton b) {
    switch (b) {
    case WsButton::A:      return KEY_A;
    case WsButton::B:      return KEY_B;
    case WsButton::X:      return KEY_X;
    case WsButton::Y:      return KEY_Y;
    case WsButton::L:      return KEY_L;
    case WsButton::R:      return KEY_R;
    case WsButton::Start:  return KEY_START;
    case WsButton::Select: return KEY_SELECT;
    }
    return 0;
}

inline u32 ws_key(WsAction a) { return ws_to_key(ws_button(a)); }

inline const char* ws_glyph(WsButton b) {
    switch (b) {
    case WsButton::A:      return "A";
    case WsButton::B:      return "B";
    case WsButton::X:      return "X";
    case WsButton::Y:      return "Y";
    case WsButton::L:      return "L";
    case WsButton::R:      return "R";
    case WsButton::Start:  return "START";
    case WsButton::Select: return "SELECT";
    }
    return "?";
}

inline const WsHintStyle& ws_hint_style() {
    static const WsHintStyle style = { &ws_glyph, ": ", "  " };
    return style;
}

inline std::string ws_hint(WsAction a, const char* label_override = nullptr) {
    return ws_format_hint(a, ws_hint_style(), label_override);
}

template <size_t N>
inline std::string ws_hint_bar(const WsAction (&acts)[N]) {
    return ws_format_hint_bar(acts, N, ws_hint_style());
}

inline std::string ws_confirm_cancel_hint() {
    static const WsAction acts[] = { WsAction::Confirm, WsAction::Cancel };
    return ws_hint_bar(acts);
}
