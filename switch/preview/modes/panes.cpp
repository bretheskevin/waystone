// pane-conflicts, pane-conflicts-confirm, pane-settings, pane-settings-update: dashboard panes
// in a ShellFrame next to a sidebar-width placeholder column.
#include "registry.h"
#include "kit_preview.h"
#include "conflicts_pane.h"
#include "settings_pane.h"

// Settings rows: Server, user, password, policy, backup, Device (skipped), Check for updates.
static constexpr int STEPS_TO_UPDATE_ROW = 5;

class PanePreviewActivity : public PreviewShellActivity {
  public:
    PanePreviewActivity(brls::Box* pane, std::string guide, std::function<void()> on_ready)
        : PreviewShellActivity(PREVIEW_READY_DELAY_MS,
                               [pane, on_ready] {
                                   brls::Application::giveFocus(pane);
                                   if (on_ready) on_ready();
                               }),
          pane_(pane), guide_(std::move(guide)) {}

  protected:
    brls::View* makeContent() override {
        auto* row = new brls::Box(brls::Axis::ROW);
        auto* sidebar = new brls::Box(brls::Axis::COLUMN);
        sidebar->setWidth(WS_SIDEBAR_W);
        sidebar->setHeight(WS_CONTENT_H);
        sidebar->setBackgroundColor(ws_nvg(WS_C_SIDEBAR));
        row->addView(sidebar);
        pane_->setMarginLeft(WS_PANE_X - WS_SIDEBAR_W);
        row->addView(pane_);
        return row;
    }

    std::string guide() const override { return guide_; }

  private:
    brls::Box*  pane_;
    std::string guide_;
};

static void press(WsAction a) {
    printf("[ui] preview: press %s\n", ws_label(a));
    preview_press(ws_brls(a));
}

static void push_conflicts(Session& session, std::function<void()> on_ready) {
    auto* pane = new ConflictsPane(&session, {});
    pane->onShown();
    brls::Application::pushActivity(new PanePreviewActivity(pane, pane->guide(), std::move(on_ready)));
}

static void seed_settings_session(Session& session) {
    preview_seed_session(session);
    session.dav.pass = "hunter2";
}

static void push_settings(Session& session, std::function<void()> on_ready) {
    seed_settings_session(session);
    auto* pane = new SettingsPane(&session);
    pane->onShown();
    brls::Application::pushActivity(new PanePreviewActivity(pane, pane->guide(), std::move(on_ready)));
}

static void run_conflicts(Session& session) { push_conflicts(session, {}); }

static void run_conflicts_confirm(Session& session) {
    push_conflicts(session, [] { press(WsAction::KeepLocal); });
}

static void run_settings(Session& session) { push_settings(session, {}); }

static void run_settings_update(Session& session) {
    push_settings(session, [] {
        for (int i = 0; i < STEPS_TO_UPDATE_ROW; i++)
            preview_press(brls::BUTTON_DOWN);
        press(WsAction::Select);
    });
}

static PreviewModeReg reg_conflicts("pane-conflicts", &run_conflicts);
static PreviewModeReg reg_conflicts_confirm("pane-conflicts-confirm", &run_conflicts_confirm);
static PreviewModeReg reg_settings("pane-settings", &run_settings);
static PreviewModeReg reg_settings_update("pane-settings-update", &run_settings_update);
