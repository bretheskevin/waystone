/*
 * Preview stub for TitleListActivity — replaces switch/source/ui/title_list_activity.cpp.
 * Defines the out-of-line virtuals so the vtable is emitted here, and renders a
 * placeholder screen if the loading→dashboard transition is ever reached in preview.
 */
#include <borealis.hpp>
#include "title_list_activity.h"

TitleListActivity::TitleListActivity(SyncController* ctrl, Session* session)
    : ctrl_(ctrl), session_(session)
{}

TitleListActivity::~TitleListActivity()
{
    delete ctrl_;
}

brls::View* TitleListActivity::createContentView()
{
    auto* box   = new brls::Box();
    auto* label = new brls::Label();
    label->setText("Title List (preview stub)");
    box->addView(label);
    return box;
}

void TitleListActivity::onContentAvailable()
{}
