#pragma once
#include "wizard_activity.h"
#include "session.h"
#include <string>
#include <vector>

class SetupActivity : public WizardActivity {
  public:
    explicit SetupActivity(Session* session);

  protected:
    std::vector<WizardStepDef> get_steps()            const override;
    std::string                finish_label()          const override;
    std::string                wizard_title()          const override;
    bool                       validate_step(size_t step) override;
    void                       on_finish()                  override;
    void                       edit_current_field()         override;

  private:
    Session* session_;

    static const size_t STEP_WELCOME    = 0;
    static const size_t STEP_SERVER     = 1;
    static const size_t STEP_USERNAME   = 2;
    static const size_t STEP_PASSWORD   = 3;
    static const size_t STEP_PASSPHRASE = 4;
    static const size_t STEP_CONFIRM    = 5;
    static const size_t NUM_STEPS       = 6;

    void do_create_vault();
};
