#include "settings_activity.h"
#include "swkbd_util.h"

SettingsActivity::SettingsActivity(Session* session) : session_(session) {}
SettingsActivity::~SettingsActivity() = default;

brls::View* SettingsActivity::createContentView()
{
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);

    auto* title = new brls::Label();
    title->setText("Settings");
    title->setFontSize(28.0f);
    title->setSingleLine(true);
    col->addView(title);

    server_label_ = new brls::Label();
    server_label_->setFontSize(20.0f);
    server_label_->registerClickAction([this](brls::View*) {
        std::string val = swkbd_prompt("Server URL", session_->config.server_url, false);
        if (!val.empty()) {
            session_->config.server_url = val;
            session_->dav.server_url = val;
            zeroize_string(session_->dav.pass);
            refresh_labels();
        }
        return true;
    });
    col->addView(server_label_);

    user_label_ = new brls::Label();
    user_label_->setFontSize(20.0f);
    user_label_->registerClickAction([this](brls::View*) {
        std::string val = swkbd_prompt("Username", session_->config.username, false);
        if (!val.empty()) {
            session_->config.username = val;
            session_->dav.user = val;
            refresh_labels();
        }
        return true;
    });
    col->addView(user_label_);

    policy_label_ = new brls::Label();
    policy_label_->setFontSize(20.0f);
    policy_label_->registerClickAction([this](brls::View*) {
        session_->config.conflict_policy =
            (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
                ? WsConflictPolicy::Prompt
                : WsConflictPolicy::NewestWins;
        refresh_labels();
        return true;
    });
    col->addView(policy_label_);

    backup_label_ = new brls::Label();
    backup_label_->setFontSize(20.0f);
    backup_label_->registerClickAction([this](brls::View*) {
        session_->config.safety_backup = !session_->config.safety_backup;
        refresh_labels();
        return true;
    });
    col->addView(backup_label_);

    device_label_ = new brls::Label();
    device_label_->setFontSize(20.0f);
    col->addView(device_label_);

    status_label_ = new brls::Label();
    status_label_->setFontSize(18.0f);
    col->addView(status_label_);

    frame->setContentView(col);
    return frame;
}

void SettingsActivity::onContentAvailable()
{
    refresh_labels();

    registerAction("Save", brls::BUTTON_A, [this](brls::View*) {
        if (wsconfig_save(session_->config, session_->config_path.c_str()))
            status_label_->setText("Settings saved");
        else
            status_label_->setText("Failed to save settings");
        return true;
    });

    registerAction("Back", brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
}

void SettingsActivity::refresh_labels()
{
    server_label_->setText("Server: " + session_->config.server_url);
    user_label_->setText("Username: " + session_->config.username);

    const char* policy_name =
        (session_->config.conflict_policy == WsConflictPolicy::Prompt) ? "Prompt" : "Newest Wins";
    policy_label_->setText(std::string("Conflict Policy: ") + policy_name);

    backup_label_->setText(std::string("Safety Backup: ")
        + (session_->config.safety_backup ? "ON" : "OFF"));

    device_label_->setText("Device ID: " + session_->device_id + " (read-only)");
}
