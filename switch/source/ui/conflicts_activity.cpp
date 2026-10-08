#include "conflicts_activity.h"
#include "borealis_focus.h"
#include "keymap_switch.h"
#include "confirm_banner.h"
#include "worker_reaper.h"
#include <cstdio>

// -----------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------
ConflictsActivity::ConflictsActivity(ConflictController* ctrl) : ctrl_(ctrl) {}

ConflictsActivity::~ConflictsActivity() {
    // Pump FIRST (mirrors WizardActivity destructor ordering).
    pump_.stop();
    poll_timer_.stop();
    reap_worker(ctrl_);
}

// -----------------------------------------------------------------------
// View construction (structural skeleton -- populated by refresh())
// -----------------------------------------------------------------------
brls::View* ConflictsActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    content_col_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->setPadding(20.0f);

    // Title (persistent -- never rebuilt)
    auto* title = new brls::Label();
    title->setText("Conflict Inbox");
    title->setFontSize(28.0f);
    title->setSingleLine(true);
    content_col_->addView(title);

    // Status label (persistent -- updated in place by poll, rebuilt by refresh)
    status_label_ = new brls::Label();
    status_label_->setFontSize(20.0f);
    content_col_->addView(status_label_);

    // List container (children rebuilt by refresh)
    list_box_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->addView(list_box_);

    frame->setContentView(content_col_);
    return frame;
}

// -----------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------
void ConflictsActivity::onContentAvailable() {
    // Initial content
    refresh();

    // Start the deferred-rebuild pump
    pump_.start();

    // Poll the controller for status changes (300 ms)
    poll_timer_.setCallback([this]() {
        // In-place status update (no rebuild needed)
        status_label_->setText("Status: " + ctrl_->status());

        uint32_t v = ctrl_->version();
        if (v != seen_version_ && !confirm_remote_) schedule_refresh();
    });
    poll_timer_.start(300);

    // --- Activity-level actions ---

    // A = Keep Local (normal mode). During confirm, A confirms the resolve.
    registerAction(ws_label(WsAction::KeepLocal), ws_brls(WsAction::KeepLocal), [this](brls::View*) {
        if (confirm_remote_) {
            printf("[conflict] ui: keep remote confirmed id=%u\n", confirm_id_);
            ctrl_->resolve_keep_remote(confirm_id_);
            confirm_remote_ = false;
            confirm_id_ = 0;
            schedule_refresh();
            return true;
        }
        size_t fi = focused_row_index();
        if (fi < items_.size() && !items_[fi].queued) {
            selected_id_ = items_[fi].id;
            printf("[conflict] ui: keep local id=%u\n", selected_id_);
            ctrl_->resolve_keep_local(selected_id_);
        }
        return true;
    });

    // X = Keep Remote (enters confirm mode)
    registerAction(ws_label(WsAction::KeepRemote), ws_brls(WsAction::KeepRemote), [this](brls::View*) {
        if (confirm_remote_) return true;  // already confirming, no-op
        size_t fi = focused_row_index();
        if (fi < items_.size() && !items_[fi].queued) {
            confirm_id_ = selected_id_ = items_[fi].id;
            confirm_remote_ = true;
            printf("[conflict] ui: keep remote requested id=%u (confirming)\n", confirm_id_);
            schedule_refresh();
        }
        return true;
    });

    // B = Back. During confirm, cancels the confirm instead.
    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        if (confirm_remote_) {
            confirm_remote_ = false;
            confirm_id_ = 0;
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
void ConflictsActivity::schedule_refresh() {
    pump_.schedule();
}

void ConflictsActivity::refresh() {
    // Capture focus by stable id BEFORE rebuilding (old children still exist).
    if (!confirm_remote_ && !items_.empty()) {
        size_t fi = focused_row_index();
        if (fi < items_.size()) { selected_id_ = items_[fi].id; focus_index_ = fi; }
    }
    // Read the version BEFORE copying so a concurrent mutation re-triggers a refresh.
    seen_version_ = ctrl_->version();
    items_ = ctrl_->views();

    rebuild_list();

    // --- Manage confirm banner ---
    if (confirm_remote_ && !banner_) {
        banner_ = build_confirm_banner();
        // Insert banner between status_label_ and list_box_.
        // content_col_ children: [0]=title, [1]=status_label_, [2]=list_box_
        // Insert at position 2 so list_box_ shifts to [3].
        content_col_->addView(banner_, 2);
    } else if (!confirm_remote_ && banner_) {
        content_col_->removeView(banner_);  // removeView frees the view
        banner_ = nullptr;
    }

    // --- Focus management ---
    // Banner is non-focusable; always restore focus to the selected list row.
    focus_selected_row();
}

// -----------------------------------------------------------------------
// List rebuild (only the list_box_ children, not the full content_col_)
// -----------------------------------------------------------------------
void ConflictsActivity::rebuild_list() {
    auto& ch = list_box_->getChildren();
    while (!ch.empty())
        list_box_->removeView(ch.front());

    const auto& items = items_;
    if (items.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No conflicts");
        empty->setFontSize(20.0f);
        list_box_->addView(empty);
        return;
    }

    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s | %s%s\n  Local: %.12s (%s)\n  Remote: %.12s (%s, dev:%s)",
                 item.title_name.c_str(), item.group_key.c_str(),
                 item.queued ? "  [Queued]" : "",
                 item.local_hash.c_str(), item.local_mtime.empty() ? "time unknown" : item.local_mtime.c_str(),
                 item.remote_hash.c_str(), item.remote_mtime.empty() ? "time unknown" : item.remote_mtime.c_str(),
                 item.remote_device_id.c_str());

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(8.0f);

        auto* label = new brls::Label();
        label->setText(buf);
        label->setFontSize(18.0f);
        if (item.queued) label->setTextColor(nvgRGB(140, 140, 140));
        label->setGrow(1.0f);
        row->addView(label);

        list_box_->addView(row);
    }
}

// -----------------------------------------------------------------------
// Determine which row is focused (d-pad selection)
// -----------------------------------------------------------------------
size_t ConflictsActivity::focused_row_index() const {
    return borealis_focused_child_index(list_box_);
}

// -----------------------------------------------------------------------
// Focus the row closest to the previously selected position
// -----------------------------------------------------------------------
void ConflictsActivity::focus_selected_row() {
    size_t idx = focus_index_;
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i].id == selected_id_) { idx = i; break; }
    borealis_focus_child(list_box_, idx, getContentView());
    focus_index_ = idx;
}

// -----------------------------------------------------------------------
// Build the confirm banner (non-focusable; actions are activity-level)
// -----------------------------------------------------------------------
brls::Box* ConflictsActivity::build_confirm_banner() {
    return make_confirm_banner("Overwrite local with remote?", "");
}

// -----------------------------------------------------------------------
// Preview support: trigger the confirm banner programmatically
// -----------------------------------------------------------------------
void ConflictsActivity::trigger_confirm_for_preview() {
    items_ = ctrl_->views();
    if (!items_.empty()) {
        selected_id_ = confirm_id_ = items_.front().id;
        focus_index_ = 0;
        confirm_remote_ = true;
        schedule_refresh();
    }
}
