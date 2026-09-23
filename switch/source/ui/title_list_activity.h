#pragma once
#include <borealis.hpp>
#include "sync_controller.h"
#include "session.h"

class TitleListActivity : public brls::Activity {
  public:
    // ctrl: owned by the activity (deleted on destruction). session: borrowed.
    TitleListActivity(SyncController* ctrl, Session* session);
    ~TitleListActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    SyncController* ctrl_;
    Session* session_;
    SyncController* single_ctrl_ = nullptr;  // owned; single-title sync (A button)

    brls::Label* status_label_ = nullptr;
    brls::RepeatingTimer poll_timer_;
    brls::Box* title_list_box_ = nullptr;

    size_t focused_title_index() const;

    void start_sync_or_gate();           // syncs all titles via ctrl_
    void start_single_sync_or_gate();    // syncs focused title only
};
