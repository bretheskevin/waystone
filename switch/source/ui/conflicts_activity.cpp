#include "conflicts_activity.h"
#include <cstdio>

ConflictsActivity::ConflictsActivity(ConflictController* ctrl) : ctrl_(ctrl) {}
ConflictsActivity::~ConflictsActivity() { poll_timer_.stop(); delete ctrl_; }

brls::View* ConflictsActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);
    auto* title = new brls::Label();
    title->setText("Conflict Inbox"); title->setFontSize(28.0f); title->setSingleLine(true);
    col->addView(title);
    status_label_ = new brls::Label(); status_label_->setFontSize(20.0f);
    col->addView(status_label_);
    list_box_ = new brls::Box(brls::Axis::COLUMN);
    col->addView(list_box_);
    frame->setContentView(col);
    return frame;
}

void ConflictsActivity::onContentAvailable() {
    poll_timer_.setCallback([this]() {
        status_label_->setText("Status: " + ctrl_->status());
        auto phase = ctrl_->phase();
        if (phase == ConflictPhase::Ready || phase == ConflictPhase::Done) rebuild_list();
    });
    poll_timer_.start(300);
    registerAction("Keep Local", brls::BUTTON_A, [this](brls::View*) {
        auto items = ctrl_->conflicts();
        if (!items.empty() && selected_index_ < items.size()) ctrl_->resolve_keep_local(selected_index_);
        return true;
    });
    registerAction("Keep Remote", brls::BUTTON_X, [this](brls::View*) {
        auto items = ctrl_->conflicts();
        if (!items.empty() && selected_index_ < items.size()) ctrl_->resolve_keep_remote(selected_index_);
        return true;
    });
    registerAction("Back", brls::BUTTON_B, [](brls::View*) { brls::Application::popActivity(); return true; });
}

void ConflictsActivity::rebuild_list() {
    // brls::Box has no clearViews(); remove each child via removeView() (which frees it).
    auto& ch = list_box_->getChildren();
    while (!ch.empty())
        list_box_->removeView(ch.front());
    auto items = ctrl_->conflicts();
    selected_index_ = 0;
    if (items.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No conflicts"); empty->setFontSize(20.0f);
        list_box_->addView(empty);
        return;
    }
    for (size_t i = 0; i < items.size(); i++) {
        const auto& item = items[i];
        char buf[512];
        snprintf(buf, sizeof(buf), "%s | %s\n  Local: %.12s (%s)\n  Remote: %.12s (%s, dev:%s)",
                 item.title_name.c_str(), item.group_key.c_str(),
                 item.local_hash.c_str(), item.local_mtime.c_str(),
                 item.remote_hash.c_str(), item.remote_mtime.c_str(), item.remote_device_id.c_str());
        auto* row = new brls::Label();
        row->setText(buf); row->setFontSize(18.0f);
        size_t idx = i;
        row->registerClickAction([this, idx](brls::View*) { selected_index_ = idx; return true; });
        list_box_->addView(row);
    }
}
