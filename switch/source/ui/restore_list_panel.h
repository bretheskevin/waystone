#pragma once
#include <borealis.hpp>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "browse_phase.h"
#include "deferred_refresh_pump.h"
#include "kit/menu_list.h"
#include "kit/overlay.h"
#include "worker_reaper.h"

struct Session;

// Shared body of the History / Snapshots slide panels: heading + game name + status line +
// a list of restorable versions; A on a row -> confirm dialog -> start_restore(i). A restore is
// refused (at A and again at confirm) while a sync, conflict worker or reaped worker runs.
// Subclasses owning a controller derive from ControllerRestorePanel (stops polling first).
class RestoreListPanel : public SlidePanel {
  public:
    ~RestoreListPanel() override;
    void onContentAvailable() override;

  protected:
    struct RowText {
        std::string title;
        std::string subtitle;
        std::string detail;
    };

    RestoreListPanel(const Session* session, std::string heading, std::string game, std::string empty_text);

    virtual size_t entryCount() const                  = 0;
    virtual std::vector<RowText> rows() const          = 0;
    virtual std::string confirmTitle() const           = 0;
    virtual std::string confirmDetail(size_t i) const  = 0;
    virtual void startRestore(size_t i)                = 0;
    virtual BrowsePhase phase() const                  = 0;
    virtual std::string status() const                 = 0;

    void buildBody(brls::Box* body) override;
    void stopPolling();
    void askRestore(size_t i);

    static constexpr float WIDTH = 640.0f;
    static constexpr const char* REPLACES_SAVE = "replaces the save on this Switch.";

  private:
    bool refuseRestore() const;
    void poll();
    void refresh();
    void rebuildList();
    void beginRestore(size_t i);

    const Session* session_;
    std::string heading_;
    std::string game_;
    std::string empty_text_;

    brls::Label* status_label_ = nullptr;
    brls::Label* empty_label_  = nullptr;
    MenuList*    list_         = nullptr;

    size_t selected_index_   = 0;
    size_t last_entry_count_ = SIZE_MAX;
    bool   restoring_        = false;
    bool   dialog_open_      = false;
    std::string shown_status_;

    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    DeferredRefreshPump   pump_{[this] { refresh(); }};
    brls::RepeatingTimer  poll_timer_;
};

// RestoreListPanel bound to the browse controller it owns (HistoryController, SnapshotsController).
template <class Ctrl>
class ControllerRestorePanel : public RestoreListPanel {
  public:
    ~ControllerRestorePanel() override {
        stopPolling();
        printf("[ui] restore panel: controller handed to reaper (running=%d)\n", (int)ctrl_->is_running());
        reap_worker(ctrl_);
    }

  protected:
    ControllerRestorePanel(Ctrl* ctrl, const Session* session, std::string heading, std::string empty_text)
        : RestoreListPanel(session, std::move(heading), ctrl->title().name, std::move(empty_text)), ctrl_(ctrl) {}

    void startRestore(size_t i) override { ctrl_->start_restore(i); }
    BrowsePhase phase() const override { return ctrl_->phase(); }
    std::string status() const override { return ctrl_->status(); }

    Ctrl* ctrl_;
};
