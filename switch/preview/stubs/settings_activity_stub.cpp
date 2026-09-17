/*
 * Preview stub for SettingsActivity.
 * No-op implementations; the Settings action is never invoked in dashboard preview.
 */
#include "settings_activity.h"

SettingsActivity::SettingsActivity(Session* session) : session_(session) {}
SettingsActivity::~SettingsActivity() {}

brls::View* SettingsActivity::createContentView() {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    auto* lbl = new brls::Label();
    lbl->setText("Settings (preview stub)");
    box->addView(lbl);
    return box;
}

void SettingsActivity::onContentAvailable() {}
void SettingsActivity::refresh_labels() {}
