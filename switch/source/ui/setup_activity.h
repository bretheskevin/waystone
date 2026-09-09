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

  private:
    Session* session_;

    static const size_t FIELD_SERVER     = 0;
    static const size_t FIELD_USERNAME   = 1;
    static const size_t FIELD_PASSWORD   = 2;
    static const size_t FIELD_PASSPHRASE = 3;
    static const size_t FIELD_CONFIRM    = 4;
    static const size_t NUM_VALUES       = 5;

    void do_create_vault();
};
