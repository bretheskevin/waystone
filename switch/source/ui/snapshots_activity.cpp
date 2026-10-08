#include "snapshots_activity.h"
#include "snapshot_browse.h" // human_size, human_timestamp
#include "borealis_focus.h"
#include "keymap_switch.h"
#include "confirm_banner.h"
#include <cstdio>

// -----------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------
SnapshotsActivity::SnapshotsActivity(SnapshotsController* ctrl) : ctrl_(ctrl) {}

SnapshotsActivity::~SnapshotsActivity() {
    pump_.stop();
    poll_timer_.stop();
    delete ctrl_;
}

// -----------------------------------------------------------------------
// View construction
// -----------------------------------------------------------------------
brls::View* SnapshotsActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    content_col_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->setPadding(20.0f);

    // Title
    title_label_ = new brls::Label();
    title_label_->setText("Snapshots: " + ctrl_->title().name);
    title_label_->setFontSize(28.0f);
    title_label_->setSingleLine(true);
    content_col_->addView(title_label_);

    // Status
    status_label_ = new brls::Label();
    status_label_->setFontSize(20.0f);
    content_col_->addView(status_label_);

    // List container
    list_box_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->addView(list_box_);

    frame->setContentView(content_col_);
    return frame;
}

// -----------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------
void SnapshotsActivity::onContentAvailable() {
    refresh();
    pump_.start();

    poll_timer_.setCallback([this]() {
        status_label_->setText("Status: " + ctrl_->status());

        auto phase = ctrl_->phase();
        if (phase == BrowsePhase::Ready || phase == BrowsePhase::Done) {
            size_t count = ctrl_->snapshots().size();
            if (count != last_snapshot_count_) {
                last_snapshot_count_ = count;
                if (!confirm_restore_) {
                    schedule_refresh();
                }
            }
        }
    });
    poll_timer_.start(300);

    // A = Restore (normal) / Confirm (confirm mode)
    registerAction(ws_label(WsAction::Restore), ws_brls(WsAction::Restore), [this](brls::View*) {
        if (confirm_restore_) {
            ctrl_->start_restore(confirm_index_);
            confirm_restore_ = false;
            schedule_refresh();
            return true;
        }
        auto items = ctrl_->snapshots();
        selected_index_ = focused_row_index();
        if (!items.empty() && selected_index_ < items.size() &&
            ctrl_->phase() == BrowsePhase::Ready) {
            confirm_index_ = selected_index_;
            confirm_restore_ = true;
            schedule_refresh();
        }
        return true;
    });

    // B = Back / Cancel confirm
    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        if (confirm_restore_) {
            confirm_restore_ = false;
            schedule_refresh();
            return true;
        }
        brls::Application::popActivity();
        return true;
    });
}

// -----------------------------------------------------------------------
// Deferred rebuild
// -----------------------------------------------------------------------
void SnapshotsActivity::schedule_refresh() {
    pump_.schedule();
}

void SnapshotsActivity::refresh() {
    if (!confirm_restore_) {
        size_t fi = focused_row_index();
        auto& ch = list_box_->getChildren();
        if (!ch.empty() && fi < ch.size())
            selected_index_ = fi;
    }

    rebuild_list();

    // Manage confirm banner
    // content_col_ children: [0]=title_label_, [1]=status_label_, [2]=list_box_
    // Insert at position 2 so list_box_ shifts to [3].
    if (confirm_restore_ && !banner_) {
        banner_ = build_confirm_banner();
        content_col_->addView(banner_, 2);
    } else if (!confirm_restore_ && banner_) {
        content_col_->removeView(banner_);
        banner_ = nullptr;
    }

    focus_selected_row();
}

// -----------------------------------------------------------------------
// List rebuild
// -----------------------------------------------------------------------
void SnapshotsActivity::rebuild_list() {
    auto& ch = list_box_->getChildren();
    while (!ch.empty())
        list_box_->removeView(ch.front());

    auto items = ctrl_->snapshots();
    if (items.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No snapshots yet for this game.");
        empty->setFontSize(20.0f);
        list_box_->addView(empty);
        return;
    }

    for (size_t i = 0; i < items.size(); i++) {
        const auto& snap = items[i];
        char buf[256];
        std::string sz = human_size(snap.total_bytes);
        snprintf(buf, sizeof(buf), "%s  |  %zu file(s), %s",
                 human_timestamp(snap.timestamp).c_str(),
                 snap.file_count, sz.c_str());

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(8.0f);

        auto* label = new brls::Label();
        label->setText(buf);
        label->setFontSize(18.0f);
        label->setGrow(1.0f);
        row->addView(label);

        list_box_->addView(row);
    }
}

// -----------------------------------------------------------------------
// Focus helpers
// -----------------------------------------------------------------------
size_t SnapshotsActivity::focused_row_index() const {
    return borealis_focused_child_index(list_box_);
}

void SnapshotsActivity::focus_selected_row() {
    borealis_focus_child(list_box_, selected_index_, getContentView());
}

// -----------------------------------------------------------------------
// Confirm banner
// -----------------------------------------------------------------------
brls::Box* SnapshotsActivity::build_confirm_banner() {
    return make_confirm_banner("Restore this snapshot?", "Current save will be backed up first.");
}
