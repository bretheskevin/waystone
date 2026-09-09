#pragma once
#include "wizard_activity.h"
#include "session.h"
#include <cstdint>
#include <string>
#include <vector>

class UnlockActivity : public WizardActivity {
  public:
    UnlockActivity(Session* session, const uint8_t* keys_data, size_t keys_len);

  protected:
    std::vector<WizardStepDef> get_steps()                const override;
    std::string                finish_label()              const override;
    std::string                wizard_title()              const override;
    bool                       validate_step(size_t step)       override;
    void                       on_finish()                      override;
    void                       register_extra_actions()         override;
    void                       edit_current_field()             override;

  private:
    Session*       session_;
    const uint8_t* keys_data_;
    size_t         keys_len_;
    bool           recovery_mode_ = false;

    static const size_t STEP_PASSPHRASE = 0;
    static const size_t STEP_PASSWORD   = 1;
    static const size_t NUM_STEPS       = 2;

    void do_unlock();
};
