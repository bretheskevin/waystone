#include "title_list_activity.h"
#include <cstdio>

TitleListActivity::TitleListActivity(SyncController* ctrl)
    : ctrl_(ctrl) {}

TitleListActivity::~TitleListActivity()
{
    poll_timer_.stop();
}

brls::View* TitleListActivity::createContentView()
{
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
    sync_label->registerClickAction([this](brls::View*) {
        ctrl_->start();
        return true;
    });
    col->addView(sync_label);

    char buf[32];
    for (const auto& t : ctrl_->titles())
    {
        snprintf(buf, sizeof(buf), "%016lX", t.title_id);
        auto* row = new brls::Label();
        row->setText(t.name + "  " + buf);
        row->setFontSize(20.0f);
        row->setSingleLine(true);
        col->addView(row);
    }

    frame->setContentView(col);
    return frame;
}

void TitleListActivity::onContentAvailable()
{
    poll_timer_.setCallback([this]() {
        status_label_->setText("Status: " + ctrl_->status());
    });
    poll_timer_.start(200);

    registerAction("Sync", brls::BUTTON_A, [this](brls::View*) {
        ctrl_->start();
        return true;
    });
}
