#include "history_activity.h"
#include "snapshot_browse.h" // human_timestamp
#include "borealis_focus.h"
#include "keymap_switch.h"
#include "confirm_banner.h"
#include <cstdio>

HistoryActivity::HistoryActivity(HistoryController* ctrl) : ctrl_(ctrl) {}

HistoryActivity::~HistoryActivity() {
    pump_.stop();
    poll_timer_.stop();
    delete ctrl_;
}

brls::View* HistoryActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    content_col_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->setPadding(20.0f);

    title_label_ = new brls::Label();
    title_label_->setText("History: " + ctrl_->title().name);
    title_label_->setFontSize(28.0f);
    title_label_->setSingleLine(true);
    content_col_->addView(title_label_);

    status_label_ = new brls::Label();
    status_label_->setFontSize(20.0f);
    content_col_->addView(status_label_);

    list_box_ = new brls::Box(brls::Axis::COLUMN);
    content_col_->addView(list_box_);

    frame->setContentView(content_col_);
    return frame;
}

void HistoryActivity::onContentAvailable() {
    refresh();
    pump_.start();

    poll_timer_.setCallback([this]() {
        status_label_->setText("Status: " + ctrl_->status());

        auto phase = ctrl_->phase();
        if (phase == BrowsePhase::Ready || phase == BrowsePhase::Done) {
            size_t count = ctrl_->entries().size();
            if (count != last_entry_count_) {
                last_entry_count_ = count;
                if (!confirm_restore_) {
                    schedule_refresh();
                }
            }
        }
    });
    poll_timer_.start(300);

    registerAction(ws_label(WsAction::Restore), ws_brls(WsAction::Restore), [this](brls::View*) {
        if (confirm_restore_) {
            ctrl_->start_restore(confirm_index_);
            confirm_restore_ = false;
            schedule_refresh();
            return true;
        }
        auto items = ctrl_->entries();
        selected_index_ = focused_row_index();
        if (!items.empty() && selected_index_ < items.size() &&
            ctrl_->phase() == BrowsePhase::Ready) {
            confirm_index_ = selected_index_;
            confirm_restore_ = true;
            schedule_refresh();
        }
        return true;
    });

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

void HistoryActivity::schedule_refresh() {
    pump_.schedule();
}

void HistoryActivity::refresh() {
    if (!confirm_restore_) {
        size_t fi = focused_row_index();
        auto& ch = list_box_->getChildren();
        if (!ch.empty() && fi < ch.size())
            selected_index_ = fi;
    }

    rebuild_list();

    // content_col_ children: [0]=title_label_, [1]=status_label_, [2]=list_box_
    // Insert banner at position 2 so list_box_ shifts to [3].
    if (confirm_restore_ && !banner_) {
        banner_ = build_confirm_banner();
        content_col_->addView(banner_, 2);
    } else if (!confirm_restore_ && banner_) {
        content_col_->removeView(banner_);
        banner_ = nullptr;
    }

    focus_selected_row();
}

void HistoryActivity::rebuild_list() {
    auto& ch = list_box_->getChildren();
    while (!ch.empty())
        list_box_->removeView(ch.front());

    auto items = ctrl_->entries();
    if (items.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No history yet for this game.");
        empty->setFontSize(20.0f);
        list_box_->addView(empty);
        return;
    }

    for (size_t i = 0; i < items.size(); i++) {
        const auto& e = items[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "%s  |  %s",
                 human_timestamp(e.timestamp).c_str(),
                 e.device_id.c_str());

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

size_t HistoryActivity::focused_row_index() const {
    return borealis_focused_child_index(list_box_);
}

void HistoryActivity::focus_selected_row() {
    borealis_focus_child(list_box_, selected_index_, getContentView());
}

brls::Box* HistoryActivity::build_confirm_banner() {
    return make_confirm_banner("Restore this version?", "Current save will be backed up first.");
}
