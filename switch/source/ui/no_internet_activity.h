#pragma once
#include <borealis.hpp>
#include <functional>
#include "net_status.h"

class NoInternetActivity : public brls::Activity {
  public:
    // on_success: called when Retry succeeds; if null, the activity just pops itself.
    NoInternetActivity(NoInternetReason reason,
                       std::function<void()> on_success = nullptr);
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    NoInternetReason reason_;
    std::function<void()> on_success_;
    brls::Label* msg_label_ = nullptr;
};
