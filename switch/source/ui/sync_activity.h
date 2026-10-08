#pragma once
#include <borealis.hpp>
#include <vector>
#include "sync_controller.h"

// Modal progress screen for one SyncController run (Sync All or single title).
// ctrl is borrowed: TitleListActivity owns it and sits below this activity.
// Every view is built once in createContentView and only mutated in place afterwards
// (setText/setTextColor/setWidth) — no rebuilds, so no focused-view UAF.
// B/A close it only once the run is Done/Error.
class SyncActivity : public brls::Activity {
  public:
    explicit SyncActivity(SyncController* ctrl);
    ~SyncActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;

  private:
    SyncController* ctrl_;
    brls::Label* title_label_ = nullptr;
    brls::Label* step_label_ = nullptr;
    brls::Label* bytes_label_ = nullptr;
    brls::Label* pct_label_ = nullptr;
    brls::Box* bar_track_ = nullptr;
    brls::Rectangle* bar_fill_ = nullptr;
    brls::Label* footer_ = nullptr;
    std::vector<brls::Label*> state_labels_;
    std::vector<int> shown_states_;
    brls::RepeatingTimer poll_timer_;
    bool finished_logged_ = false;

    bool finished() const;
    void poll();
    void close_if_finished(const char* why);
};
