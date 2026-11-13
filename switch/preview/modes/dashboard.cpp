// Dashboard preview modes: dashboard, dashboard-sidebar, dashboard-emu, dashboard-settings,
// game-panel. Input is driven through the public controller entry point after the push.
#include "registry.h"
#include "kit_preview.h"
#include <initializer_list>
#include <vector>
#include "title_list_activity.h"


static void push_dashboard(Session& session, std::initializer_list<brls::ControllerButton> presses) {
    preview_seed_session(session);
    brls::Application::pushActivity(new TitleListActivity(preview_sync_controller(&session.config), &session));
    preview_press_after(PREVIEW_READY_DELAY_MS, std::vector<brls::ControllerButton>(presses));
}

static void run_dashboard(Session& s) {
    push_dashboard(s, {brls::BUTTON_RIGHT, brls::BUTTON_RIGHT, brls::BUTTON_RIGHT});
}
static void run_dashboard_sidebar(Session& s) { push_dashboard(s, {brls::BUTTON_DOWN, brls::BUTTON_DOWN}); }
static void run_dashboard_emu(Session& s) {
    push_dashboard(s, {brls::BUTTON_DOWN, ws_brls(WsAction::Select)});
}
static void run_dashboard_settings(Session& s) {
    push_dashboard(s, {brls::BUTTON_DOWN, brls::BUTTON_DOWN, brls::BUTTON_DOWN});
}
static void run_game_panel(Session& s) {
    push_dashboard(s, {brls::BUTTON_RIGHT, brls::BUTTON_RIGHT, brls::BUTTON_RIGHT,
                       ws_brls(WsAction::OpenGame)});
}

static PreviewModeReg reg_dashboard("dashboard", &run_dashboard);
static PreviewModeReg reg_dashboard_sidebar("dashboard-sidebar", &run_dashboard_sidebar);
static PreviewModeReg reg_dashboard_emu("dashboard-emu", &run_dashboard_emu);
static PreviewModeReg reg_dashboard_settings("dashboard-settings", &run_dashboard_settings);
static PreviewModeReg reg_game_panel("game-panel", &run_game_panel);
