// Setup vault-step modes: setup-join, setup-join-recovery, setup-join-plain, setup-plain,
// setup-plain-encrypt, setup-probe-error. They start on the filled WebDAV Login step and press
// Next, so the real probe -> step switch path runs against the stubbed probe
// (preview/stubs/vault_remote_stub.cpp).
#include "registry.h"
#include "kit_preview.h"
#include "setup_activity.h"
#include <vector>

extern RemoteKeysState g_preview_probe_state;
extern int             g_preview_probe_status;
extern bool            g_preview_probe_plain;

static constexpr int PREVIEW_AFTER_STEP_MS = 3500;  // > the 2 s PREVIEW_SLOW_TRANSITION

static void push_login(Session& session, RemoteKeysState probe, int status, bool plain = false) {
    g_preview_probe_state  = probe;
    g_preview_probe_status = status;
    g_preview_probe_plain  = plain;
    preview_seed_session(session);
    auto* act = new PreviewLoginSetupActivity(&session);
    act->preset_login("hunter2");
    brls::Application::pushActivity(act);
    preview_press_after(PREVIEW_READY_DELAY_MS, {ws_brls(WsAction::WizardNext)});
}

static void run_setup_join(Session& s) { push_login(s, RemoteKeysState::Present, 200); }

static void run_setup_join_recovery(Session& s) {
    push_login(s, RemoteKeysState::Present, 200);
    preview_press_after(PREVIEW_READY_DELAY_MS + PREVIEW_AFTER_STEP_MS,
                {ws_brls(WsAction::ToggleRecovery)});
}

static void run_setup_probe_error(Session& s) { push_login(s, RemoteKeysState::Error, -1); }

static void run_setup_join_plain(Session& s) { push_login(s, RemoteKeysState::Present, 200, true); }

static void run_setup_plain(Session& s) { push_login(s, RemoteKeysState::Absent, 404); }

static void run_setup_plain_encrypt(Session& s) {
    push_login(s, RemoteKeysState::Absent, 404);
    preview_press_after(PREVIEW_READY_DELAY_MS + PREVIEW_AFTER_STEP_MS,
                {ws_brls(WsAction::ToggleEncryption)});
}

static PreviewModeReg reg_setup_join("setup-join", &run_setup_join);
static PreviewModeReg reg_setup_join_recovery("setup-join-recovery", &run_setup_join_recovery);
static PreviewModeReg reg_setup_probe_error("setup-probe-error", &run_setup_probe_error);
static PreviewModeReg reg_setup_join_plain("setup-join-plain", &run_setup_join_plain);
static PreviewModeReg reg_setup_plain("setup-plain", &run_setup_plain);
static PreviewModeReg reg_setup_plain_encrypt("setup-plain-encrypt", &run_setup_plain_encrypt);
