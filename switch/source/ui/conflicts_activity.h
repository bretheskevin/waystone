#pragma once
#include <borealis.hpp>
#include "conflict_controller.h"
#include "deferred_refresh_pump.h"

class ConflictsActivity : public brls::Activity {
  public:
    explicit ConflictsActivity(ConflictController* ctrl);
    ~ConflictsActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

    // Preview support: trigger the confirm banner programmatically.
    // Called by the preview subclass; harmless in shipped builds.
    void trigger_confirm_for_preview();

  private:
    ConflictController* ctrl_;

    // Persistent views (survive across refresh)
    brls::Box*   content_col_   = nullptr;
    brls::Label* status_label_  = nullptr;
    brls::Box*   list_box_      = nullptr;
    brls::Box*   banner_        = nullptr;

    // Confirm state
    bool   confirm_remote_     = false;
    size_t confirm_index_      = 0;  // index captured when X was pressed

    // Selection tracking (logical cursor, mirrors 3DS cursor_ model)
    size_t selected_index_     = 0;

    // Change detection
    size_t last_conflict_count_ = SIZE_MAX;  // force first rebuild

    // Deferred rebuild: never rebuild the focused view synchronously inside
    // its own action callback (UAF). ConflictsActivity has its own poll_timer_
    // for status polling; only the rebuild is delegated to the pump.
    DeferredRefreshPump pump_{ [this]{ refresh(); } };

    brls::RepeatingTimer poll_timer_;

    void schedule_refresh();
    void refresh();
    void rebuild_list();
    size_t focused_row_index() const;
    brls::Box* build_confirm_banner();
    void focus_selected_row();
};
