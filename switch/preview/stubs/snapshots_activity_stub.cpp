/*
 * Preview stub for SnapshotsActivity.
 * No-op implementations; snapshot actions are never invoked in dashboard preview.
 */
#include "snapshots_activity.h"

SnapshotsActivity::SnapshotsActivity(SnapshotsController* ctrl) : ctrl_(ctrl) {}

SnapshotsActivity::~SnapshotsActivity() {
    pump_.stop();
    poll_timer_.stop();
    delete ctrl_;
}

brls::View* SnapshotsActivity::createContentView() {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    auto* lbl = new brls::Label();
    lbl->setText("Snapshots (preview stub)");
    box->addView(lbl);
    return box;
}

void SnapshotsActivity::onContentAvailable() {}

void SnapshotsActivity::schedule_refresh() {}
void SnapshotsActivity::refresh() {}
void SnapshotsActivity::rebuild_list() {}
size_t SnapshotsActivity::focused_row_index() const { return 0; }
brls::Box* SnapshotsActivity::build_confirm_banner() { return new brls::Box(); }
void SnapshotsActivity::focus_selected_row() {}
