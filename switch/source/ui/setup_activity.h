#pragma once
#include "wizard_activity.h"
#include "vault_helpers.h"
#include "session.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

class SetupActivity : public WizardActivity {
  public:
    explicit SetupActivity(Session* session);
    ~SetupActivity() override;

  protected:
    std::vector<WizardStepDef> get_steps()            const override;
    std::string                finish_label()          const override;
    std::string                wizard_title()          const override;
    bool                       validate_step(size_t step) override;
    void                       on_finish()                  override;

    // Set while the vault worker thread is running; suppresses re-entry.
    bool creating_vault_ = false;

  private:
    Session* session_;

    static const size_t FIELD_SERVER     = 0;
    static const size_t FIELD_USERNAME   = 1;
    static const size_t FIELD_PASSWORD   = 2;
    static const size_t FIELD_PASSPHRASE = 3;
    static const size_t FIELD_CONFIRM    = 4;
    static const size_t NUM_VALUES       = 5;

    // Worker-thread state; all read/written exclusively via vault_done_ ordering.
    std::atomic<bool> vault_done_{false};
    VaultCreateResult vault_result_;
    std::thread       vault_thread_;

    void do_create_vault();
};
