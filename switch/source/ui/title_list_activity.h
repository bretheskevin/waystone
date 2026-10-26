#pragma once
#include <borealis.hpp>
#include <vector>
#include "dashboard_model.h"
#include "deferred_refresh_pump.h"
#include "session.h"
#include "sync_controller.h"

class ConflictsPane;
class GamesPane;
class IconSidebar;
class SettingsPane;
class ShellFrame;

// Dashboard: ShellFrame[ IconSidebar(Games, Emulator, Conflicts, Settings) | pane host ].
// Focusing a sidebar item swaps the pane; Select/Right enters it, Back returns to the sidebar.
class TitleListActivity : public brls::Activity {
  public:
    // ctrl: owned by the activity (deleted on destruction). session: borrowed.
    TitleListActivity(SyncController* ctrl, Session* session);
    ~TitleListActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    enum class FocusArea { None, Sidebar, Grid, Conflicts, Settings };

    FocusArea focus_area() const;
    brls::View* pane_view(Section s) const;
    void apply_section();
    void enter_pane();
    void poll();
    void refresh_guide(FocusArea area);
    void apply_finished_runs();
    void open_game(const TitleInfo& t);

    void start_sync_or_gate();                          // syncs all titles via ctrl_
    void start_single_sync_or_gate(const TitleInfo& t); // syncs one title via single_ctrl_
    bool single_sync_running() const;
    bool sync_running() const;
    // True (logged + toasted) while a conflict worker or a reaped History/Snapshots worker may
    // still write saves: a sync started then would race it on the same save data.
    bool background_task_blocks_sync(const char* what) const;

    SyncController* ctrl_;
    Session* session_;
    SyncController* single_ctrl_ = nullptr;   // owned; single-title sync (GamePanel)
    SyncController* pending_modal_ = nullptr; // borrowed; modal to push once the dashboard is on top
    bool full_run_pending_   = false;         // results not yet applied to the grids
    bool single_run_pending_ = false;

    ShellFrame*    frame_     = nullptr;
    IconSidebar*   sidebar_   = nullptr;
    brls::Box*     host_      = nullptr;
    GamesPane*     games_     = nullptr;
    GamesPane*     emulator_  = nullptr;
    ConflictsPane* conflicts_ = nullptr;
    SettingsPane*  settings_  = nullptr;

    Section   shown_       = Section::Games;
    Section   target_      = Section::Games;
    FocusArea last_area_   = FocusArea::None;
    int       last_badge_  = -1;
    std::vector<TitleResult> results_;  // by title index; latest outcome per title

    brls::RepeatingTimer poll_timer_;
    DeferredRefreshPump  pump_{[this] { apply_section(); }};
};
