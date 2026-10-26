#pragma once
#include <borealis.hpp>
#include "session.h"
#include "deferred_refresh_pump.h"
#include "update_controller.h"
#include <string>

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
    brls::Label* password_label_ = nullptr;
    brls::Label* policy_label_  = nullptr;
    brls::Label* backup_label_  = nullptr;
    brls::Label* device_label_  = nullptr;
    brls::Label* logout_label_  = nullptr;
    brls::Label* update_label_  = nullptr;
    brls::Label* version_label_ = nullptr;
    brls::Label* save_label_    = nullptr;
    brls::Label* status_label_  = nullptr;
    void refresh_labels();
    void save_settings();
    void do_logout();
    bool logout_pending_ = false;
    void poll_update();
    void refresh_banner();
    void start_install();   // confirm banner accepted: download + install pending_ver_
    void restart_now();     // restart banner accepted: envSetNextLoad + quit
    UpdateController* updater_ = nullptr;
    brls::Box* col_    = nullptr;
    brls::Box* banner_ = nullptr;
    bool confirm_update_ = false;  // banner: "update available, install now?"
    bool restart_prompt_ = false;  // banner: "installed, restart now?"
    bool banner_active() const { return confirm_update_ || restart_prompt_; }
    bool install_started_ = false;
    UpdatePhase last_phase_ = UpdatePhase::Idle;
    std::string pending_ver_;
    DeferredRefreshPump pump_{[this] { do_logout(); refresh_banner(); }, [this] { poll_update(); }};
};
