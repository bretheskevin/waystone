#pragma once
#include <borealis.hpp>
#include "session.h"

class SetupActivity : public brls::Activity {
  public:
    explicit SetupActivity(Session* session);
    ~SetupActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    Session* session_;
    brls::Label* status_label_ = nullptr;
    void run_setup();
};
