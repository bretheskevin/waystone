#pragma once
#include <cstddef>
#include <string>

enum class WsButton { A, B, X, Y, L, R, Start, Select };

enum class WsAction {
    Confirm, Cancel, Back, Select,
    SyncGame, SyncAll, OpenConflicts, OpenSettings, OpenSnapshots, OpenHistory,
    KeepLocal, KeepRemote, Restore,
    WizardEdit, WizardNext, WizardPrev, WizardPrevAlt,
    ToggleRecovery, Retry, ExitApp, Continue, Update, Quit,
    Count
};

struct WsBinding { WsAction action; WsButton button; const char* label; };

static const size_t WS_ACTION_COUNT = static_cast<size_t>(WsAction::Count);

// Indexed by WsAction; order MUST match the enum (test_keymap asserts it).
inline const WsBinding* ws_binding_table() {
    static const WsBinding table[] = {
        { WsAction::Confirm,        WsButton::A,      "Confirm" },
        { WsAction::Cancel,         WsButton::B,      "Cancel" },
        { WsAction::Back,           WsButton::B,      "Back" },
        { WsAction::Select,         WsButton::A,      "Select" },
        { WsAction::SyncGame,       WsButton::A,      "Sync Game" },
        { WsAction::SyncAll,        WsButton::Select, "Sync All" },
        { WsAction::OpenConflicts,  WsButton::X,      "Conflicts" },
        { WsAction::OpenSettings,   WsButton::Y,      "Settings" },
        { WsAction::OpenSnapshots,  WsButton::L,      "Snapshots" },
        { WsAction::OpenHistory,    WsButton::R,      "History" },
        { WsAction::KeepLocal,      WsButton::A,      "Keep Local" },
        { WsAction::KeepRemote,     WsButton::X,      "Keep Remote" },
        { WsAction::Restore,        WsButton::A,      "Restore" },
        { WsAction::WizardEdit,     WsButton::A,      "Edit" },
        { WsAction::WizardNext,     WsButton::R,      "Next" },
        { WsAction::WizardPrev,     WsButton::B,      "Back" },
        { WsAction::WizardPrevAlt,  WsButton::L,      "Back" },
        { WsAction::ToggleRecovery, WsButton::Y,      "Toggle Recovery" },
        { WsAction::Retry,          WsButton::A,      "Retry" },
        { WsAction::ExitApp,        WsButton::B,      "Exit" },
        { WsAction::Continue,       WsButton::A,      "Continue" },
        { WsAction::Update,         WsButton::A,      "Update now" },
        { WsAction::Quit,           WsButton::Start,  "Quit" },
    };
    static_assert(sizeof(table) / sizeof(table[0]) == WS_ACTION_COUNT,
                  "keymap table must cover every WsAction");
    return table;
}

inline const WsBinding& ws_binding(WsAction a) {
    size_t i = static_cast<size_t>(a);
    if (i >= WS_ACTION_COUNT) i = 0;
    return ws_binding_table()[i];
}
inline WsButton    ws_button(WsAction a) { return ws_binding(a).button; }
inline const char* ws_label(WsAction a)  { return ws_binding(a).label; }

typedef const char* (*WsGlyphFn)(WsButton);
struct WsHintStyle { WsGlyphFn glyph; const char* glyph_sep; const char* item_sep; };

inline std::string ws_format_hint(WsAction a, const WsHintStyle& st, const char* label_override) {
    std::string s = st.glyph(ws_button(a));
    s += st.glyph_sep;
    s += (label_override && label_override[0]) ? label_override : ws_label(a);
    return s;
}

inline std::string ws_format_hint_bar(const WsAction* acts, size_t n, const WsHintStyle& st) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (i) s += st.item_sep;
        s += ws_format_hint(acts[i], st, nullptr);
    }
    return s;
}
