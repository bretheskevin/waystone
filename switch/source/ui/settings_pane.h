#pragma once
#include <borealis.hpp>
#include <string>
#include "deferred_refresh_pump.h"
#include "session.h"
#include "update_flow.h"

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
    void save_settings();
    // Workers borrow the vault (History also reads the Session's WebDAV strings): no logout
    // or credential edit while one runs. Logs + toasts the refusal.
    bool refuse_while_busy(const char* what) const;
    void request_logout();
    void do_logout();

    Session* session_;

    MenuList* list_         = nullptr;
    MenuRow*  server_row_   = nullptr;
    MenuRow*  user_row_     = nullptr;
    MenuRow*  password_row_ = nullptr;
    MenuRow*  policy_row_   = nullptr;
    MenuRow*  backup_row_   = nullptr;
    MenuRow*  device_row_   = nullptr;
    MenuRow*  update_row_   = nullptr;

    bool logout_pending_ = false;

    UpdateFlow update_;  // status sink = update_row_ detail

    DeferredRefreshPump pump_{[this] { do_logout(); }, [this] { update_.tick(); }};
};
