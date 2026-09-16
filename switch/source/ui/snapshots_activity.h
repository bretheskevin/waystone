#pragma once
#include <borealis.hpp>
#include "snapshots_controller.h"
#include "deferred_refresh_pump.h"

class SnapshotsActivity : public brls::Activity {
  public:
    explicit SnapshotsActivity(SnapshotsController* ctrl);
    ~SnapshotsActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    SnapshotsController* ctrl_;

    // Persistent views
    brls::Box*   content_col_   = nullptr;
    brls::Label* title_label_   = nullptr;
    brls::Label* status_label_  = nullptr;
    brls::Box*   list_box_      = nullptr;
    brls::Box*   banner_        = nullptr;

    // Confirm state
    bool   confirm_restore_     = false;
    size_t confirm_index_       = 0;

    // Selection tracking
    size_t selected_index_      = 0;

    // Change detection
    size_t last_snapshot_count_ = SIZE_MAX;

    // Deferred rebuild pump
    DeferredRefreshPump pump_{ [this]{ refresh(); } };

    brls::RepeatingTimer poll_timer_;

    void schedule_refresh();
    void refresh();
    void rebuild_list();
    size_t focused_row_index() const;
    brls::Box* build_confirm_banner();
    void focus_selected_row();
};
