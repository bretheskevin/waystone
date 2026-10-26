#pragma once
#include <borealis.hpp>
#include <vector>
#include "kit/overlay.h"
#include "sync_controller.h"

// Progress dialog for one SyncController run (Sync All or single title). Push with
// OverlayActivity::push. ctrl is borrowed: TitleListActivity owns it and sits below.
// Every view is built once in buildBody and only mutated in place afterwards
// (setText/setTextColor/setWidth) — no rebuilds, so no focused-view UAF.
// B/A close it only once the run is Done/Error; closing toasts the final status.
class SyncActivity : public DialogCard {
  public:
    explicit SyncActivity(SyncController* ctrl);
    ~SyncActivity() override;
    void onContentAvailable() override;

  protected:
    void buildBody(brls::Box* body) override;

  private:
    SyncController* ctrl_;
    brls::Label* title_label_ = nullptr;
    brls::Label* step_label_ = nullptr;
    brls::Label* bytes_label_ = nullptr;
    brls::Label* pct_label_ = nullptr;
    brls::Box* bar_fill_ = nullptr;
    std::vector<brls::Label*> state_labels_;
    std::vector<int> shown_states_;
    brls::RepeatingTimer poll_timer_;
    bool finished_logged_ = false;

    bool finished() const;
    void poll();
    void close_if_finished(const char* why);
};
