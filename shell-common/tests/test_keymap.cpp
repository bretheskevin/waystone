// Host-only test for the shared controller keymap.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_keymap.cpp -o /tmp/test_keymap && /tmp/test_keymap
#include "keymap.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void test_every_action_has_binding() {
    for (size_t i = 0; i < WS_ACTION_COUNT; i++) {
        WsAction a = static_cast<WsAction>(i);
        const WsBinding& b = ws_binding(a);
        assert(b.action == a);
        assert(b.label != nullptr && b.label[0] != '\0');
    }
    printf("test_every_action_has_binding PASSED\n");
}

static void assert_unique_buttons(const char* screen, const WsAction* acts, size_t n) {
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i + 1; j < n; j++) {
            if (ws_button(acts[i]) == ws_button(acts[j])) {
                printf("button clash on %s: '%s' vs '%s'\n",
                       screen, ws_label(acts[i]), ws_label(acts[j]));
                assert(false);
            }
        }
    }
}
#define CHECK_SCREEN(arr) assert_unique_buttons(#arr, arr, sizeof(arr) / sizeof(arr[0]))

static void test_no_button_clash_per_screen() {
    using A = WsAction;
    static const A dashboard[]       = { A::SyncGame, A::SyncAll, A::OpenConflicts, A::OpenSettings,
                                         A::OpenSnapshots, A::OpenHistory, A::Quit };
    static const A conflicts[]       = { A::KeepLocal, A::KeepRemote, A::Back, A::Quit };
    static const A confirm_banner[]  = { A::Confirm, A::Cancel, A::Quit };
    static const A restore_browse[]  = { A::Restore, A::Back, A::Quit };
    static const A settings[]        = { A::Select, A::Back, A::Quit };
    static const A updater_banner[]  = { A::Update, A::Cancel, A::Quit };
    static const A wizard[]          = { A::WizardEdit, A::WizardNext, A::WizardPrev,
                                         A::WizardPrevAlt, A::Quit };
    static const A unlock[]          = { A::WizardEdit, A::WizardNext, A::WizardPrev,
                                         A::WizardPrevAlt, A::ToggleRecovery, A::Quit };
    static const A recovery_key[]    = { A::Continue, A::Quit };
    static const A no_internet[]     = { A::Retry, A::ExitApp, A::Quit };
    static const A sync_progress[]   = { A::Continue, A::Quit };
    static const A loading_error[]   = { A::Back, A::Quit };
    CHECK_SCREEN(dashboard);
    CHECK_SCREEN(conflicts);
    CHECK_SCREEN(confirm_banner);
    CHECK_SCREEN(restore_browse);
    CHECK_SCREEN(settings);
    CHECK_SCREEN(updater_banner);
    CHECK_SCREEN(wizard);
    CHECK_SCREEN(unlock);
    CHECK_SCREEN(recovery_key);
    CHECK_SCREEN(no_internet);
    CHECK_SCREEN(sync_progress);
    CHECK_SCREEN(loading_error);
    printf("test_no_button_clash_per_screen PASSED\n");
}

static void test_decided_bindings() {
    assert(ws_button(WsAction::SyncAll)       == WsButton::Select);
    assert(ws_button(WsAction::WizardEdit)    == WsButton::A);
    assert(ws_button(WsAction::WizardNext)    == WsButton::R);
    assert(ws_button(WsAction::WizardPrev)    == WsButton::B);
    assert(ws_button(WsAction::WizardPrevAlt) == WsButton::L);
    assert(ws_button(WsAction::Quit)          == WsButton::Start);
    assert(ws_button(WsAction::KeepRemote)    == WsButton::X);
    assert(ws_button(WsAction::OpenSnapshots) == WsButton::L);
    assert(ws_button(WsAction::OpenHistory)   == WsButton::R);
    assert(ws_button(WsAction::ToggleRecovery)== WsButton::Y);
    assert(ws_button(WsAction::Confirm)       == WsButton::A);
    assert(ws_button(WsAction::Cancel)        == WsButton::B);
    assert(strcmp(ws_label(WsAction::SyncAll), "Sync All") == 0);
    assert(strcmp(ws_label(WsAction::WizardPrev), ws_label(WsAction::WizardPrevAlt)) == 0);
    printf("test_decided_bindings PASSED\n");
}

static const char* fake_glyph(WsButton b) {
    switch (b) {
    case WsButton::A: return "<A>";
    case WsButton::B: return "<B>";
    case WsButton::R: return "<R>";
    default:          return "<?>";
    }
}

static void test_format_hint() {
    WsHintStyle st = { &fake_glyph, ": ", " | " };
    assert(ws_format_hint(WsAction::Retry, st, nullptr) == "<A>: Retry");
    assert(ws_format_hint(WsAction::WizardNext, st, "Create Vault") == "<R>: Create Vault");
    assert(ws_format_hint(WsAction::WizardNext, st, "") == "<R>: Next");
    const WsAction bar[] = { WsAction::Retry, WsAction::ExitApp };
    assert(ws_format_hint_bar(bar, 2, st) == "<A>: Retry | <B>: Exit");
    assert(ws_format_hint_bar(bar, 0, st).empty());
    printf("test_format_hint PASSED\n");
}

int main() {
    test_every_action_has_binding();
    test_no_button_clash_per_screen();
    test_decided_bindings();
    test_format_hint();
    printf("ALL keymap tests PASSED\n");
    return 0;
}
