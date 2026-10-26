#pragma once
#include "wizard_activity.h"
#include "setup_model.h"
#include "vault_helpers.h"
#include "session.h"
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// Leaving the login step probes the server: an existing vault turns the last step into
// "Join your vault", otherwise it creates one and publishes it.
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
    void                       register_extra_actions()     override;
    bool                       nav_locked()            const override { return busy_; }

    // A probe / create / join worker is running (main thread only).
    bool           busy_          = false;
    SetupVaultMode mode_          = SetupVaultMode::Create;
    bool           join_recovery_ = false;

    static const size_t FIELD_SERVER     = 0;
    static const size_t FIELD_USERNAME   = 1;
    static const size_t FIELD_PASSWORD   = 2;
    static const size_t FIELD_PASSPHRASE = 3;
    static const size_t FIELD_CONFIRM    = 4;
    static const size_t NUM_VALUES       = 5;
    static const size_t STEP_LOGIN       = 2;
    static const size_t STEP_VAULT       = 3;

  private:
    Session* session_;
    bool     probe_passed_ = false;

    // Written by the worker, read on the main thread once worker_done_ is set. While busy_
    // the worker reads values_ directly (edits/navigation are locked), so no secret copies.
    RemoteKeys        probe_{RemoteKeysState::Absent, {}, 0};
    VaultSetupResult  setup_result_{nullptr, "", {}, ""};
    std::atomic<bool> worker_done_{false};
    std::thread       worker_;

    void run_worker(std::function<void()> work, std::function<void()> on_done);
    void start_probe();
    void on_probe_done();
    void start_setup();
    void on_setup_done(bool join);
};
