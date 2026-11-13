// Pre-login action modes: unlock-forget (X -> forget-server confirm), unlock-update (Select ->
// stubbed check at v9.9.9 -> status + install dialog), setup-prefilled (login step pre-filled
// from session.config, as after Forget server).
#include "registry.h"
#include "kit_preview.h"
#include "setup_activity.h"
#include "unlock_activity.h"

static void push_plain_unlock(Session& session, WsAction press) {
    preview_seed_session(session);
    size_t len = 0;
    const uint8_t* keys = preview_plain_keys(&len);
    brls::Application::pushActivity(new UnlockActivity(&session, keys, len));
    preview_press_after(PREVIEW_READY_DELAY_MS, {ws_brls(press)});
}

static void run_unlock_forget(Session& s) { push_plain_unlock(s, WsAction::ForgetServer); }

static void run_unlock_update(Session& s) { push_plain_unlock(s, WsAction::CheckUpdates); }

static void run_setup_prefilled(Session& session) {
    preview_seed_session(session);
    auto* act = new PreviewLoginSetupActivity(&session);
    act->preset_login();
    brls::Application::pushActivity(act);
}

static PreviewModeReg reg_unlock_forget("unlock-forget", &run_unlock_forget);
static PreviewModeReg reg_unlock_update("unlock-update", &run_unlock_update);
static PreviewModeReg reg_setup_prefilled("setup-prefilled", &run_setup_prefilled);
