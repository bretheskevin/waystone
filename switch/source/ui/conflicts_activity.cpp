#include "conflicts_activity.h"
#include <cstdio>

// -----------------------------------------------------------------------
// RefreshPump -- fires on every borealis frame (~16 ms).
// Only does work when refresh_pending_ is set, ensuring the rebuild
// runs AFTER input dispatch unwinds (avoiding the focused-view UAF).
// -----------------------------------------------------------------------
void ConflictsActivity::RefreshPump::run() {
    if (owner_->refresh_pending_) {
        owner_->refresh_pending_ = false;
        owner_->refresh();
    }
}

// -----------------------------------------------------------------------
// Construction / destruction
// -----------------------------------------------------------------------
ConflictsActivity::ConflictsActivity(ConflictController* ctrl) : ctrl_(ctrl) {}

ConflictsActivity::~ConflictsActivity() {
    // Pump FIRST (mirrors WizardActivity destructor ordering).
    if (refresh_pump_) {
        refresh_pump_->stop();
        delete refresh_pump_;
        refresh_pump_ = nullptr;
    }
    poll_timer_.stop();
    delete ctrl_;
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
    refresh_pump_ = new RefreshPump(this);
    refresh_pump_->start();

    // Poll the controller for status changes (300 ms)
    poll_timer_.setCallback([this]() {
        // In-place status update (no rebuild needed)
        status_label_->setText("Status: " + ctrl_->status());

        auto phase = ctrl_->phase();
        if (phase == ConflictPhase::Ready || phase == ConflictPhase::Done) {
            size_t count = ctrl_->conflicts().size();
            if (count != last_conflict_count_) {
                last_conflict_count_ = count;
                // Do NOT rebuild while the confirm banner is showing
                if (!confirm_remote_) {
                    schedule_refresh();
                }
            }
        }
    });
    poll_timer_.start(300);

    // --- Activity-level actions ---

    // A = Keep Local (normal mode). During confirm, A confirms the resolve.
    registerAction("Keep Local", brls::BUTTON_A, [this](brls::View*) {
        if (confirm_remote_) {
            ctrl_->resolve_keep_remote(confirm_index_);
            confirm_remote_ = false;
            schedule_refresh();
            return true;
        }
        auto items = ctrl_->conflicts();
        selected_index_ = focused_row_index();
        if (!items.empty() && selected_index_ < items.size()) {
            ctrl_->resolve_keep_local(selected_index_);
            schedule_refresh();
        }
        return true;
    });

    // X = Keep Remote (enters confirm mode)
    registerAction("Keep Remote", brls::BUTTON_X, [this](brls::View*) {
        if (confirm_remote_) return true;  // already confirming, no-op
        auto items = ctrl_->conflicts();
        selected_index_ = focused_row_index();
        if (!items.empty() && selected_index_ < items.size()) {
            confirm_index_ = selected_index_;
            confirm_remote_ = true;
            schedule_refresh();
        }
        return true;
    });

    // B = Back. During confirm, cancels the confirm instead.
    registerAction("Back", brls::BUTTON_B, [this](brls::View*) {
        if (confirm_remote_) {
            confirm_remote_ = false;
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
    refresh_pending_ = true;
}

void ConflictsActivity::refresh() {
    // Capture current focus position BEFORE rebuilding (old children still exist).
    // In confirm mode, selected_index_ was already set by the X action.
    if (!confirm_remote_) {
        size_t fi = focused_row_index();
        auto& ch = list_box_->getChildren();
        if (!ch.empty() && fi < ch.size())
            selected_index_ = fi;
    }

    // --- Rebuild list rows ---
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

    auto items = ctrl_->conflicts();
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
                 "%s | %s\n  Local: %.12s (%s)\n  Remote: %.12s (%s, dev:%s)",
                 item.title_name.c_str(), item.group_key.c_str(),
                 item.local_hash.c_str(), item.local_mtime.c_str(),
                 item.remote_hash.c_str(), item.remote_mtime.c_str(),
                 item.remote_device_id.c_str());

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
// Determine which row is focused (d-pad selection)
// -----------------------------------------------------------------------
size_t ConflictsActivity::focused_row_index() const {
    auto* focus = brls::Application::getCurrentFocus();
    if (!focus) return 0;
    auto& ch = list_box_->getChildren();
    for (size_t i = 0; i < ch.size(); i++) {
        // Check if focus is the row itself or a child of the row
        brls::View* v = focus;
        while (v) {
            if (v == ch[i]) return i;
            v = v->getParent();
        }
    }
    return 0;  // fallback to first row
}

// -----------------------------------------------------------------------
// Focus the row closest to the previously selected position
// -----------------------------------------------------------------------
void ConflictsActivity::focus_selected_row() {
    auto& ch = list_box_->getChildren();
    if (ch.empty()) {
        // Nothing to focus -- give focus to the content view
        if (auto* cv = getContentView())
            brls::Application::giveFocus(cv);
        return;
    }
    // Clamp selected_index_ to the new list size (items may have been resolved)
    if (selected_index_ >= ch.size())
        selected_index_ = ch.size() - 1;
    auto* row = ch[selected_index_];
    if (row->isFocusable())
        brls::Application::giveFocus(row);
    else if (auto* cv = getContentView())
        brls::Application::giveFocus(cv);
}

// -----------------------------------------------------------------------
// Build the confirm banner (non-focusable; actions are activity-level)
// -----------------------------------------------------------------------
brls::Box* ConflictsActivity::build_confirm_banner() {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(12.0f);
    box->setMargins(12.0f, 0.0f, 12.0f, 0.0f);
    // Warning background (amber/orange)
    box->setBackgroundColor(nvgRGBA(0xFF, 0xAA, 0x00, 0xFF));

    auto* line1 = new brls::Label();
    line1->setText("Overwrite local with remote?");
    line1->setFontSize(22.0f);
    line1->setTextColor(nvgRGB(0x1A, 0x1A, 0x1A));
    line1->setSingleLine(true);
    box->addView(line1);

    auto* line2 = new brls::Label();
    line2->setText("A: Confirm   B: Cancel");
    line2->setFontSize(18.0f);
    line2->setTextColor(nvgRGB(0x33, 0x33, 0x33));
    line2->setSingleLine(true);
    box->addView(line2);

    return box;
}

// -----------------------------------------------------------------------
// Preview support: trigger the confirm banner programmatically
// -----------------------------------------------------------------------
void ConflictsActivity::trigger_confirm_for_preview() {
    auto items = ctrl_->conflicts();
    if (!items.empty()) {
        selected_index_ = 0;
        confirm_index_ = 0;
        confirm_remote_ = true;
        schedule_refresh();
    }
}
