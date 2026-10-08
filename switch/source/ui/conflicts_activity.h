#pragma once
#include <borealis.hpp>
#include <cstdint>
#include <vector>
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

    // List snapshot (view copies, keyed by stable id) and confirm state
    std::vector<ConflictView> items_;
    uint32_t seen_version_  = UINT32_MAX;  // force first rebuild
    uint32_t selected_id_   = 0;
    uint32_t confirm_id_    = 0;
    bool     confirm_remote_ = false;
    size_t   focus_index_   = 0;

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
