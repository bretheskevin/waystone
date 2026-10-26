#pragma once
#include <borealis.hpp>
#include <memory>
#include <string>
#include "deferred_refresh_pump.h"
#include "session.h"
#include "update_controller.h"

class MenuList;
class MenuRow;

// Dashboard Settings pane: one MenuList row per setting (title + value detail). Update offers
// and restart prompts are ConfirmDialogs. Back/Quit belong to the dashboard.
class SettingsPane : public brls::Box {
  public:
    explicit SettingsPane(Session* session);
    ~SettingsPane() override;

    void onShown();
    std::string guide() const;

  private:
    void refresh_labels();
    void edit_server();
    void edit_user();
    void edit_password();
    void toggle_policy();
    void toggle_backup();
    void check_updates();
    void save_settings();
    // Workers borrow the vault and the WebDAV strings (WebDavCfg holds their c_str()): no logout
    // or credential edit while one runs. Logs + toasts the refusal.
    bool refuse_while_busy(const char* what) const;
    void request_logout();
    void do_logout();
    void poll_update();
    void offer_install();
    void offer_restart();
    void start_install();
    void restart_now();
    void set_update_status(const std::string& text);

    Session*          session_;
    UpdateController* updater_ = nullptr;

    MenuList* list_         = nullptr;
    MenuRow*  server_row_   = nullptr;
    MenuRow*  user_row_     = nullptr;
    MenuRow*  password_row_ = nullptr;
    MenuRow*  policy_row_   = nullptr;
    MenuRow*  backup_row_   = nullptr;
    MenuRow*  device_row_   = nullptr;
    MenuRow*  update_row_   = nullptr;

    bool        logout_pending_  = false;
    bool        install_started_ = false;
    bool        dialog_open_     = false;
    UpdatePhase last_phase_      = UpdatePhase::Idle;
    std::string pending_ver_;

    // Dialog callbacks run after the dialog is deleted; they bail out once the pane is gone.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    DeferredRefreshPump pump_{[this] { do_logout(); }, [this] { poll_update(); }};
};
