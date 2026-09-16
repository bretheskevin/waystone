#pragma once
#include <borealis.hpp>
#include "history_controller.h"
#include "deferred_refresh_pump.h"

class HistoryActivity : public brls::Activity {
  public:
    explicit HistoryActivity(HistoryController* ctrl);
    ~HistoryActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    HistoryController* ctrl_;

    brls::Box*   content_col_   = nullptr;
    brls::Label* title_label_   = nullptr;
    brls::Label* status_label_  = nullptr;
    brls::Box*   list_box_      = nullptr;
    brls::Box*   banner_        = nullptr;

    bool   confirm_restore_     = false;
    size_t confirm_index_       = 0;

    size_t selected_index_      = 0;

    size_t last_entry_count_    = SIZE_MAX;

    DeferredRefreshPump pump_{ [this]{ refresh(); } };

    brls::RepeatingTimer poll_timer_;

    void schedule_refresh();
    void refresh();
    void rebuild_list();
    size_t focused_row_index() const;
    brls::Box* build_confirm_banner();
    void focus_selected_row();
};
