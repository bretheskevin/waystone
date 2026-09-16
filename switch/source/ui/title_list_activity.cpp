#include "title_list_activity.h"
#include "settings_activity.h"
#include "conflicts_activity.h"
#include "conflict_controller.h"
#include "no_internet_activity.h"
#include "net_status.h"
#include "snapshots_controller.h"
#include "snapshots_activity.h"
#include "history_controller.h"
#include "history_activity.h"
#include "borealis_focus.h"
#include <cstdio>

TitleListActivity::TitleListActivity(SyncController* ctrl, Session* session)
    : ctrl_(ctrl), session_(session) {}

TitleListActivity::~TitleListActivity() {
    poll_timer_.stop();
    if (ctrl_) { ctrl_->join(); delete ctrl_; }
}

brls::View* TitleListActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);
    status_label_ = new brls::Label();
    status_label_->setText("Status: Idle");
    status_label_->setFontSize(24.0f);
    status_label_->setSingleLine(true);
    col->addView(status_label_);
    auto* sync_label = new brls::Label();
    sync_label->setText("Sync all");
    sync_label->setFontSize(24.0f);
    sync_label->setSingleLine(true);
    sync_label->registerClickAction([this](brls::View*) { start_sync_or_gate(); return true; });
    col->addView(sync_label);
    char buf[32];
    title_list_box_ = new brls::Box(brls::Axis::COLUMN);
    for (const auto& t : ctrl_->titles()) {
        snprintf(buf, sizeof(buf), "%016lX", t.title_id);
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(8.0f);
        auto* label = new brls::Label();
        label->setText(t.name + "  " + buf);
        label->setFontSize(20.0f);
        label->setSingleLine(true);
        label->setGrow(1.0f);
        row->addView(label);
        title_list_box_->addView(row);
    }
    col->addView(title_list_box_);
    frame->setContentView(col);
    return frame;
}

void TitleListActivity::onContentAvailable() {
    poll_timer_.setCallback([this]() { status_label_->setText("Status: " + ctrl_->status()); });
    poll_timer_.start(200);
    registerAction("Sync", brls::BUTTON_A, [this](brls::View*) { start_sync_or_gate(); return true; });
    registerAction("Conflicts", brls::BUTTON_X, [this](brls::View*) {
        auto titles = ctrl_->titles();
        auto* cc = new ConflictController(session_->vault, session_->uid, session_->device_id,
                                          session_->dav.as_cfg(), titles);
        cc->start_scan();
        brls::Application::pushActivity(new ConflictsActivity(cc));
        return true;
    });
    registerAction("Settings", brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::pushActivity(new SettingsActivity(session_));
        return true;
    });
    registerAction("Snapshots", brls::BUTTON_LB, [this](brls::View*) {
        auto titles = ctrl_->titles();
        size_t idx = focused_title_index();
        if (idx < titles.size()) {
            auto* sc = new SnapshotsController(titles[idx], session_->uid);
            sc->start_scan();
            brls::Application::pushActivity(new SnapshotsActivity(sc));
        }
        return true;
    });
    registerAction("History", brls::BUTTON_RB, [this](brls::View*) {
        auto titles = ctrl_->titles();
        size_t idx = focused_title_index();
        if (idx < titles.size()) {
            auto* hc = new HistoryController(titles[idx], session_);
            hc->start_scan();
            brls::Application::pushActivity(new HistoryActivity(hc));
        }
        return true;
    });
}

void TitleListActivity::start_sync_or_gate() {
    if (!network_available()) {
        brls::Application::pushActivity(
            new NoInternetActivity(NoInternetReason::NoNetwork, [this]() {
                ctrl_->start();
            }));
        return;
    }
    ctrl_->start();
}

size_t TitleListActivity::focused_title_index() const {
    if (!title_list_box_) return 0;
    return borealis_focused_child_index(title_list_box_);
}
