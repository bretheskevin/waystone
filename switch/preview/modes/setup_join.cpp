// Setup vault-join modes: setup-join, setup-join-recovery, setup-probe-error. They start on the
// filled WebDAV Login step and press Next, so the real probe -> step switch path runs against
// the stubbed probe (preview/stubs/vault_remote_stub.cpp).
#include "registry.h"
#include "kit_preview.h"
#include "setup_activity.h"
#include <vector>

extern RemoteKeysState g_preview_probe_state;
extern int             g_preview_probe_status;

class PreviewLoginSetupActivity : public SetupActivity {
  public:
    explicit PreviewLoginSetupActivity(Session* s) : SetupActivity(s) {}

    // Call BEFORE pushActivity(): borealis calls onContentAvailable() synchronously inside it.
    void preset_login() {
        values_[FIELD_SERVER]   = "https://dav.example.com";
        values_[FIELD_USERNAME] = "waystone";
        values_[FIELD_PASSWORD] = "hunter2";
        current_step_           = STEP_LOGIN;
    }
};

static constexpr int PREVIEW_AFTER_STEP_MS = 3500;  // > the 2 s PREVIEW_SLOW_TRANSITION

static void press_after(int delay_ms, std::vector<brls::ControllerButton> seq) {
    auto* timer = new brls::Timer();
    timer->setEndCallback([seq](bool) {
        for (brls::ControllerButton b : seq) preview_press(b);
    });
    timer->start(delay_ms);
}

static void push_login(Session& session, RemoteKeysState probe, int status) {
    g_preview_probe_state  = probe;
    g_preview_probe_status = status;
    preview_seed_session(session);
    auto* act = new PreviewLoginSetupActivity(&session);
    act->preset_login();
    brls::Application::pushActivity(act);
    press_after(PREVIEW_READY_DELAY_MS, {ws_brls(WsAction::WizardNext)});
}

static void run_setup_join(Session& s) { push_login(s, RemoteKeysState::Present, 200); }

static void run_setup_join_recovery(Session& s) {
    push_login(s, RemoteKeysState::Present, 200);
    press_after(PREVIEW_READY_DELAY_MS + PREVIEW_AFTER_STEP_MS,
                {ws_brls(WsAction::ToggleRecovery)});
}

static void run_setup_probe_error(Session& s) { push_login(s, RemoteKeysState::Error, -1); }

static PreviewModeReg reg_setup_join("setup-join", &run_setup_join);
static PreviewModeReg reg_setup_join_recovery("setup-join-recovery", &run_setup_join_recovery);
static PreviewModeReg reg_setup_probe_error("setup-probe-error", &run_setup_probe_error);
