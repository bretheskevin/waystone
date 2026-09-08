#pragma once
#include <borealis.hpp>
#include "conflict_controller.h"

class ConflictsActivity : public brls::Activity {
  public:
    explicit ConflictsActivity(ConflictController* ctrl);
    ~ConflictsActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    ConflictController* ctrl_;
    brls::Label* status_label_ = nullptr;
    brls::Box* list_box_ = nullptr;
    brls::RepeatingTimer poll_timer_;
    size_t selected_index_ = 0;
    void rebuild_list();
};
