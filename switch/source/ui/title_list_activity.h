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
    brls::Label* status_label_ = nullptr;
    brls::RepeatingTimer poll_timer_;
};
