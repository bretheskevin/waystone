#pragma once
#include "screen.h"
#include "wizard.h"
#include "session.h"
#include "vault_helpers.h"
#include <3ds.h>
#include <atomic>
#include <cstdint>
#include <vector>

class SetupScreen : public Screen {
public:
    SetupScreen(Session* session);
    ~SetupScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
    enum Job { JOB_NONE, JOB_PROBE, JOB_CREATE, JOB_JOIN };
private:
    Session* session_;
    bool joining_;   // last step joins the server's vault instead of creating one
    bool recovery_;  // join step takes the recovery key instead of the passphrase
    Wizard wizard_;
    Job job_; std::atomic<bool> job_done_; Thread job_thread_;
    RemoteKeys probe_;
    std::vector<uint8_t> server_keys_;
    VaultSetupResult result_;
    std::string recovery_path_;
    float spinner_angle_;
    static const size_t FIELD_SERVER=0, FIELD_USERNAME=1, FIELD_PASSWORD=2, FIELD_PASSPHRASE=3, FIELD_CONFIRM=4, NUM_VALUES=5;
    static const size_t STEP_SERVER=1, STEP_LOGIN=2, STEP_VAULT=3;
    std::vector<WizardStepDef> make_steps() const;
    void set_vault_mode(bool joining);
    SetupVaultMode vault_mode() const { return joining_ ? SetupVaultMode::Join : SetupVaultMode::Create; }
    bool validate_step(size_t step);
    void start_job(Job job);
    void on_probe_done();
    void on_vault_done(Job job);
    static void job_thread_entry(void* arg);
    SetupScreen(const SetupScreen&); SetupScreen& operator=(const SetupScreen&);
};
