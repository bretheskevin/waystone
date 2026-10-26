// Panels over a dashboard stand-in: history-panel, history-confirm, snapshots-panel,
// sync-dialog (alias sync-running), sync-dialog-done.
#include "registry.h"
#include "kit_preview.h"
#include "game_panel.h"
#include "history_panel.h"
#include "snapshots_panel.h"
#include "sync_activity.h"
#include "kit/title_grid.h"
#include "kit/ws_scroll_frame.h"
#include "session.h"

extern bool g_preview_sync_running;
extern bool g_preview_sync_done;

static const char* const FIXTURE_GAME = "The Legend of Zelda: TotK";

// ShellFrame base whose content is a dummy title grid.
class PanelsBaseActivity : public PreviewShellActivity {
  public:
    using PreviewShellActivity::PreviewShellActivity;

  protected:
    brls::View* makeContent() override {
        WsScrollFrame* scroll = ws_make_scroll(WS_PANE_W);
        scroll->setMarginLeft(WS_PANE_X);
        scroll->setHeight(WS_CONTENT_H);
        auto* grid = new TitleGrid();
        for (size_t i = 0; i < 14; i++) {
            auto* tile = new TitleTile();
            tile->setArt(FIXTURE_ICON, ws_nvg(WS_C_PLACEHOLDER));
            grid->addTile(tile);
        }
        scroll->setContentView(grid);
        return scroll;
    }
};

class PreviewHistoryPanel : public HistoryPanel {
  public:
    using HistoryPanel::HistoryPanel;
    using RestoreListPanel::askRestore;
};

static TitleInfo fixture_title() {
    TitleInfo ti;
    ti.title_id = 0x0100F2C0115B6000ULL;
    ti.name     = FIXTURE_GAME;
    return ti;
}

static GamePanel* fixture_game_panel(Session* s) {
    return new GamePanel(fixture_title(), s, "Not synced yet", [](TitleInfo) {});
}

static void run_history(Session& session, bool confirm) {
    Session* s = &session;
    brls::Application::pushActivity(new PanelsBaseActivity(PREVIEW_READY_DELAY_MS, [s, confirm] {
        OverlayActivity::push(fixture_game_panel(s));
        auto* hc = new HistoryController(fixture_title(), s);
        hc->start_scan();
        auto* panel = new PreviewHistoryPanel(hc, s);
        OverlayActivity::push(panel);
        if (confirm) panel->askRestore(0);
    }));
}

static void run_history_panel(Session& session) { run_history(session, false); }
static void run_history_confirm(Session& session) { run_history(session, true); }

static void run_snapshots_panel(Session& session) {
    Session* s = &session;
    brls::Application::pushActivity(new PanelsBaseActivity(PREVIEW_READY_DELAY_MS, [s] {
        OverlayActivity::push(fixture_game_panel(s));
        auto* sc = new SnapshotsController(fixture_title(), AccountUid{});
        sc->start_scan();
        OverlayActivity::push(new SnapshotsPanel(sc, s));
    }));
}

static void run_sync(Session& session, bool done) {
    g_preview_sync_running = !done;
    g_preview_sync_done    = done;
    const WaystoneShellConfig* cfg = &session.config;
    brls::Application::pushActivity(new PanelsBaseActivity(PREVIEW_READY_DELAY_MS, [cfg] {
        OverlayActivity::push(new SyncActivity(preview_sync_controller(cfg)));
    }));
}

static void run_sync_dialog(Session& session) { run_sync(session, false); }
static void run_sync_dialog_done(Session& session) { run_sync(session, true); }

static PreviewModeReg reg_history_panel("history-panel", &run_history_panel);
static PreviewModeReg reg_history_confirm("history-confirm", &run_history_confirm);
static PreviewModeReg reg_snapshots_panel("snapshots-panel", &run_snapshots_panel);
static PreviewModeReg reg_sync_dialog("sync-dialog", &run_sync_dialog);
static PreviewModeReg reg_sync_running("sync-running", &run_sync_dialog);  // legacy capture name
static PreviewModeReg reg_sync_dialog_done("sync-dialog-done", &run_sync_dialog_done);
