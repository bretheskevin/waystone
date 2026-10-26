#include "title_list_activity.h"
#include "borealis_focus.h"
#include "conflicts_pane.h"
#include "game_panel.h"
#include "games_pane.h"
#include "keymap_switch.h"
#include "net_status.h"
#include "no_internet_activity.h"
#include "settings_pane.h"
#include "sync_activity.h"
#include "worker_reaper.h"
#include "kit/icon_sidebar.h"
#include "kit/shell_frame.h"
#include "kit/title_grid.h"
#include "kit/toast.h"
#include <cstdio>

static constexpr int POLL_MS = 200;

static const char* section_name(Section s) {
    switch (s) {
    case Section::Games:     return "games";
    case Section::Emulator:  return "emulator";
    case Section::Conflicts: return "conflicts";
    case Section::Settings:  return "settings";
    }
    return "?";
}

static bool same_title(const TitleInfo& a, const TitleInfo& b) {
    if (a.source != b.source) return false;
    return a.source == SourceRom ? a.rom_id == b.rom_id : a.title_id == b.title_id;
}

namespace {

// Hosts the four panes (one VISIBLE, the rest GONE). Box focus routing doesn't skip GONE
// children, so focus enters the active pane only and never moves between panes.
class PaneHost : public brls::Box {
  public:
    explicit PaneHost(std::function<brls::View*()> active) : active_(std::move(active)) {}
    brls::View* getDefaultFocus() override {
        brls::View* pane = active_();
        return pane ? pane->getDefaultFocus() : nullptr;
    }
    brls::View* getNextFocus(brls::FocusDirection, brls::View*) override { return nullptr; }

  private:
    std::function<brls::View*()> active_;
};

}

TitleListActivity::TitleListActivity(SyncController* ctrl, Session* session)
    : ctrl_(ctrl), session_(session), results_(ctrl->titles().size()) {}

TitleListActivity::~TitleListActivity() {
    session_->sync_busy = nullptr;
    session_->sync_running = nullptr;
    pump_.stop();
    poll_timer_.stop();
    if (single_ctrl_) { single_ctrl_->join(); delete single_ctrl_; }
    if (ctrl_) { ctrl_->join(); delete ctrl_; }
}

brls::View* TitleListActivity::createContentView() {
    printf("[ui] dashboard createContentView: start\n");
    const auto& titles = ctrl_->titles();

    frame_ = new ShellFrame("Waystone");
    auto* row = new brls::Box(brls::Axis::ROW);

    sidebar_ = new IconSidebar();
    sidebar_->setHeight(WS_CONTENT_H);
    sidebar_->addItem(WS_ICON_GAMES, "Games");
    sidebar_->addItem(WS_ICON_EMULATOR, "Emulator");
    sidebar_->addItem(WS_ICON_CONFLICTS, "Conflicts");
    sidebar_->addItem(WS_ICON_SETTINGS, "Settings");
    sidebar_->setActive(static_cast<size_t>(Section::Games));
    sidebar_->setOnFocusItem([this](size_t item) {
        const Section s = static_cast<Section>(item);
        if (s == target_) return;
        printf("[ui] dashboard: sidebar focus -> %s (pane swap scheduled)\n", section_name(s));
        target_ = s;
        sidebar_->setActive(item);
        pump_.schedule();
    });
    sidebar_->registerAction(ws_label(WsAction::Select), ws_brls(WsAction::Select), [this](brls::View*) {
        enter_pane();
        return true;
    });
    row->addView(sidebar_);

    // Entering the host (Right / Select) applies a pane swap the pump has not run yet.
    host_ = new PaneHost([this] {
        apply_section();
        return pane_view(shown_);
    });
    host_->setMarginLeft(WS_PANE_X - WS_SIDEBAR_W);
    host_->setWidth(WS_PANE_W);
    host_->setHeight(WS_CONTENT_H);
    host_->registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        printf("[ui] dashboard: back -> sidebar (%s)\n", section_name(shown_));
        brls::Application::giveFocus(sidebar_);
        return true;
    });

    auto on_open = [this](const TitleInfo& t) { open_game(t); };
    games_ = new GamesPane(titles, Section::Games);
    games_->setOnOpen(on_open);
    emulator_ = new GamesPane(titles, Section::Emulator);
    emulator_->setOnOpen(on_open);
    conflicts_ = new ConflictsPane(session_, titles);
    settings_  = new SettingsPane(session_);
    for (brls::Box* pane : {static_cast<brls::Box*>(games_), static_cast<brls::Box*>(emulator_),
                            static_cast<brls::Box*>(conflicts_), static_cast<brls::Box*>(settings_)}) {
        if (pane != games_) pane->setVisibility(brls::Visibility::GONE);
        host_->addView(pane);
    }
    row->addView(host_);

    frame_->setContent(row);
    printf("[ui] dashboard createContentView: done (%zu titles)\n", titles.size());
    return frame_;
}

void TitleListActivity::onContentAvailable() {
    printf("[ui] dashboard onContentAvailable\n");
    session_->sync_busy = [this]() { return sync_running() || conflicts_->busy(); };
    session_->sync_running = [this]() { return sync_running(); };
    frame_->guide()->slideIn();
    refresh_guide(FocusArea::Sidebar);
    pump_.start();
    poll_timer_.setCallback([this]() { poll(); });
    poll_timer_.start(POLL_MS);
    poll();

    registerAction(ws_label(WsAction::SyncAll), ws_brls(WsAction::SyncAll),
                   [this](brls::View*) { start_sync_or_gate(); return true; });
}

brls::View* TitleListActivity::pane_view(Section s) const {
    switch (s) {
    case Section::Games:     return games_;
    case Section::Emulator:  return emulator_;
    case Section::Conflicts: return conflicts_;
    case Section::Settings:  return settings_;
    }
    return nullptr;
}

void TitleListActivity::apply_section() {
    if (target_ == shown_) return;
    printf("[ui] dashboard: pane swap %s -> %s\n", section_name(shown_), section_name(target_));
    pane_view(shown_)->setVisibility(brls::Visibility::GONE);
    shown_ = target_;
    pane_view(shown_)->setVisibility(brls::Visibility::VISIBLE);
    if (shown_ == Section::Conflicts) conflicts_->onShown();
    if (shown_ == Section::Settings) settings_->onShown();
}

void TitleListActivity::enter_pane() {
    brls::View* focus = host_->getDefaultFocus();
    if (!focus) {
        printf("[ui] dashboard: enter %s pane: nothing focusable, stay on sidebar\n", section_name(shown_));
        return;
    }
    printf("[ui] dashboard: enter %s pane\n", section_name(shown_));
    brls::Application::giveFocus(focus);
}

TitleListActivity::FocusArea TitleListActivity::focus_area() const {
    brls::View* f = brls::Application::getCurrentFocus();
    if (!f || !frame_ || !borealis_is_descendant(f, frame_)) return FocusArea::None;
    if (borealis_is_descendant(f, sidebar_)) return FocusArea::Sidebar;
    if (borealis_is_descendant(f, conflicts_)) return FocusArea::Conflicts;
    if (borealis_is_descendant(f, settings_)) return FocusArea::Settings;
    if (borealis_is_descendant(f, games_) || borealis_is_descendant(f, emulator_)) return FocusArea::Grid;
    return FocusArea::None;
}

void TitleListActivity::refresh_guide(FocusArea area) {
    const std::string sep = ws_hint_style().item_sep;
    switch (area) {
    case FocusArea::Sidebar:
        frame_->setGuide(ws_hint_bar({WsAction::Select, WsAction::SyncAll, WsAction::Quit}));
        break;
    case FocusArea::Grid:
        frame_->setGuide(ws_hint(WsAction::OpenGame) + sep + ws_hint_pair(WsAction::JumpUp, WsAction::JumpDown) +
                         sep + ws_hint_bar({WsAction::SyncAll, WsAction::Back, WsAction::Quit}));
        break;
    case FocusArea::Conflicts: frame_->setGuide(conflicts_->guide()); break;
    case FocusArea::Settings:  frame_->setGuide(settings_->guide()); break;
    case FocusArea::None:      break;
    }
}

static const char* area_name(int a) {
    static const char* const names[] = {"none", "sidebar", "grid", "conflicts", "settings"};
    return names[a];
}

void TitleListActivity::poll() {
    const FocusArea area = focus_area();
    if (area != last_area_) {
        printf("[ui] dashboard: focus area %s -> %s\n", area_name((int)last_area_), area_name((int)area));
        last_area_ = area;
    }
    refresh_guide(area);

    // Focus comes back synchronously on pop, before the popped activity leaves the stack; only
    // !isHidden() proves an opaque one (NoInternet) is gone. Overlays on top hold the focus.
    if (pending_modal_ && !isHidden() && area != FocusArea::None) {
        SyncController* c = pending_modal_;
        pending_modal_ = nullptr;
        printf("[ui] dashboard on top again -> push deferred sync modal\n");
        OverlayActivity::push(new SyncActivity(c));
    }

    apply_finished_runs();

    const int badge = conflict_badge(conflicts_->knownConflicts(), results_);
    if (badge != last_badge_) {
        last_badge_ = badge;
        sidebar_->setBadge(static_cast<size_t>(Section::Conflicts), badge);
    }

    const size_t game_count = ctrl_->titles().size();
    frame_->setStatus((single_sync_running() ? single_ctrl_->status() : ctrl_->status()) + " \xc2\xb7 " +
                      std::to_string(game_count) + " game" + (game_count != 1 ? "s" : ""));
}

static bool run_finished(const SyncController* c) {
    const SyncPhase p = c->phase();
    return p == SyncPhase::Done || p == SyncPhase::Error;
}

void TitleListActivity::apply_finished_runs() {
    bool changed = false;
    if (full_run_pending_ && run_finished(ctrl_)) {
        full_run_pending_ = false;
        const std::vector<TitleResult> r = ctrl_->results();
        for (size_t i = 0; i < r.size() && i < results_.size(); i++) results_[i] = r[i];
        printf("[sync] sync all finished -> %zu results applied to the grids\n", r.size());
        changed = true;
    }
    if (single_run_pending_ && single_ctrl_ && run_finished(single_ctrl_)) {
        single_run_pending_ = false;
        const std::vector<TitleResult> r = single_ctrl_->results();
        const auto& one = single_ctrl_->titles();
        const auto& all = ctrl_->titles();
        bool mapped = false;
        for (size_t i = 0; !r.empty() && !one.empty() && i < all.size(); i++) {
            if (!same_title(all[i], one[0])) continue;
            results_[i] = r[0];
            mapped = true;
            printf("[sync] sync single finished -> '%s' (title %zu) = %s\n", all[i].name.c_str(), i,
                   title_state_label(r[0].state));
            break;
        }
        if (!mapped) printf("[sync] sync single finished: result not mapped to a dashboard title\n");
        changed = changed || mapped;
    }
    if (!changed) return;
    games_->setResults(results_);
    emulator_->setResults(results_);
}

void TitleListActivity::open_game(const TitleInfo& t) {
    GamesPane* pane = shown_ == Section::Emulator ? emulator_ : games_;
    printf("[ui] dashboard: open game panel '%s'\n", t.name.c_str());
    OverlayActivity::push(new GamePanel(t, session_, pane->focusedDetail(),
                                        [this](TitleInfo picked) { start_single_sync_or_gate(picked); }));
}

bool TitleListActivity::single_sync_running() const {
    return single_ctrl_ && single_ctrl_->phase() == SyncPhase::Running;
}

bool TitleListActivity::sync_running() const {
    return ctrl_->phase() == SyncPhase::Running || single_sync_running();
}

bool TitleListActivity::background_task_blocks_sync(const char* what) const {
    // A sync writes saves exactly like a restore: same refusal rules.
    return refuse_if_busy(BusyScope::Restore, session_, "[sync]", what);
}

static void refuse_sync_running(const char* what, const char* why) {
    printf("[sync] %s refused: %s\n", what, why);
    ws_toast("A sync is already running");
}

void TitleListActivity::start_sync_or_gate() {
    printf("[ui] sync all triggered (%zu titles)\n", ctrl_->titles().size());
    if (sync_running()) {
        refuse_sync_running("sync all", "a sync is already running");
        return;
    }
    if (background_task_blocks_sync("sync all")) return;
    if (!network_available()) {
        // NoInternetActivity pops itself then calls back: never push from that callback.
        brls::Application::pushActivity(
            new NoInternetActivity(NoInternetReason::NoNetwork, [this]() {
                ctrl_->start();
                full_run_pending_ = true;
                pending_modal_ = ctrl_;
                printf("[ui] sync all: modal deferred until dashboard is on top again\n");
            }));
        return;
    }
    printf("[sync] sync all: start\n");
    ctrl_->start();
    full_run_pending_ = true;
    OverlayActivity::push(new SyncActivity(ctrl_));
}

void TitleListActivity::start_single_sync_or_gate(const TitleInfo& t) {
    if (ctrl_->phase() == SyncPhase::Running) {
        refuse_sync_running("sync single", "full sync in progress");
        return;
    }
    printf("[sync] sync single: %s (id=%016llx)\n", t.name.c_str(), (unsigned long long)t.title_id);

    // Refused while a single-title sync is already running (don't block UI thread with join).
    if (single_sync_running()) {
        refuse_sync_running("sync single", "already running");
        return;
    }
    if (background_task_blocks_sync("sync single")) return;
    if (single_ctrl_) {
        apply_finished_runs();  // don't drop a finished run the 200 ms poll has not applied yet
        if (pending_modal_ == single_ctrl_) {
            printf("[ui] sync single: dropping the deferred modal of the previous run\n");
            pending_modal_ = nullptr;
        }
        single_ctrl_->join();
        delete single_ctrl_;
    }
    single_ctrl_ = new SyncController(session_->vault, session_->uid, session_->device_id,
                                      &session_->dav, { t }, &session_->config);

    if (!network_available()) {
        brls::Application::pushActivity(
            new NoInternetActivity(NoInternetReason::NoNetwork, [this]() {
                single_ctrl_->start();
                single_run_pending_ = true;
                pending_modal_ = single_ctrl_;
                printf("[ui] sync single: modal deferred until dashboard is on top again\n");
            }));
        return;
    }
    single_ctrl_->start();
    single_run_pending_ = true;
    OverlayActivity::push(new SyncActivity(single_ctrl_));
}
