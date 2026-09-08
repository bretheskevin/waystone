#pragma once
#include <borealis.hpp>
#include "session.h"

class UnlockActivity : public brls::Activity {
  public:
    UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len);
    ~UnlockActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    Session* session_;
    const uint8_t* keys_data_;
    size_t keys_len_;
    brls::Label* status_label_ = nullptr;
    bool recovery_mode_ = false;
    void attempt_unlock();
};
