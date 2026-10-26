#pragma once
#include <borealis.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "conflict_controller.h"
#include "deferred_refresh_pump.h"
#include "session.h"

class MenuList;

// Dashboard Conflicts pane: scan status + one two-line row per conflict. A (KeepLocal) / X
// (KeepRemote) on a row open a ConfirmDialog. Back/Quit belong to the dashboard.
class ConflictsPane : public brls::Box {
  public:
    ConflictsPane(Session* session, std::vector<TitleInfo> titles);
    ~ConflictsPane() override;

    // (Re)starts a scan unless one is already running.
    void onShown();
    // -1 until a scan has finished, then the number of open conflicts.
    int knownConflicts() const { return known_; }
    // True while the scan/resolve worker (which borrows the session vault) runs.
    bool busy() const { return ctrl_ && ctrl_->is_running(); }
    std::string guide() const;

  private:
    void refresh();
    void rebuild_list();
    void focus_selected_row();
    void confirm_resolve(const ConflictView& item, bool keep_local);

    Session*               session_;
    std::vector<TitleInfo> titles_;
    ConflictController*    ctrl_ = nullptr;

    brls::Label* status_label_ = nullptr;
    brls::View*  scroll_       = nullptr;
    MenuList*    list_         = nullptr;
    brls::Box*   empty_        = nullptr;

    std::vector<ConflictView> items_;
    uint32_t seen_version_ = UINT32_MAX;
    uint32_t selected_id_  = 0;
    size_t   focus_index_  = 0;
    int      known_        = -1;
    // While a ConfirmDialog is up its focus stack holds one of our rows: never rebuild then.
    bool     dialog_open_  = false;

    // Dialog callbacks run after the dialog is deleted; they bail out once the pane is gone.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    DeferredRefreshPump  pump_{[this] { refresh(); }};
    brls::RepeatingTimer poll_timer_;
};
