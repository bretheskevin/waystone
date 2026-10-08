#pragma once
#include <borealis.hpp>
#include "session.h"

class SettingsActivity : public brls::Activity {
  public:
    explicit SettingsActivity(Session* session);
    ~SettingsActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    Session* session_;
    brls::Label* server_label_  = nullptr;
    brls::Label* user_label_    = nullptr;
    brls::Label* policy_label_  = nullptr;
    brls::Label* backup_label_  = nullptr;
    brls::Label* device_label_  = nullptr;
    brls::Label* logout_label_  = nullptr;
    brls::Label* save_label_    = nullptr;
    brls::Label* status_label_  = nullptr;
    void refresh_labels();
    void save_settings();
};
