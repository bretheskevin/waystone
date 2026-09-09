#pragma once
#include <borealis.hpp>
#include "session.h"
#include <string>

class RecoveryKeyActivity : public brls::Activity {
  public:
    RecoveryKeyActivity(Session* session,
                        const std::string& recovery_hex,
                        const std::string& recovery_path);
    ~RecoveryKeyActivity() override;
    brls::View* createContentView() override;
    void onContentAvailable() override;
  private:
    Session* session_;
    std::string recovery_hex_;
    std::string recovery_path_;
};
