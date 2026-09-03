#pragma once

#include <borealis.hpp>
#include "sync_controller.h"

class TitleListActivity : public brls::Activity
{
  public:
    explicit TitleListActivity(SyncController* ctrl);
    ~TitleListActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    SyncController* ctrl_;
    brls::Label* status_label_ = nullptr;
    brls::RepeatingTimer poll_timer_;
};
