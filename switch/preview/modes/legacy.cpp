// Pre-kit preview modes: setup-welcome, setup-field, creating-vault, unlock, recovery,
// no-internet, transition, loading.
#include "registry.h"
#include "kit_preview.h"
#include <borealis.hpp>
#include "setup_activity.h"
#include "unlock_activity.h"
#include "recovery_key_activity.h"
#include "no_internet_activity.h"
#include "loading_activity.h"
#include "session.h"
#include <chrono>
#include <string>
#include <thread>

// Subclass exposes protected WizardActivity members without modifying shipped source.
class PreviewSetupActivity : public SetupActivity {
public:
    explicit PreviewSetupActivity(Session* s) : SetupActivity(s) {}

    // Call BEFORE pushActivity(): borealis calls onContentAvailable() synchronously inside it.
    void preset_step1() {
        values_[0]    = "https://dav.example.com";  // FIELD_SERVER = 0
        current_step_ = 1;
        error_.clear();
    }

    void preset_creating_vault() {
        values_[0]       = "https://dav.example.com";  // FIELD_SERVER
        values_[1]       = "waystone";                  // FIELD_USERNAME
        current_step_    = 3;
        creating_vault_  = true;
        status_          = "Creating vault\xe2\x80\xa6 please wait";
    }
};

// Auto-advances step 0 -> 1 after 4 s so the transition animation can be captured.
class PreviewTransitionActivity : public SetupActivity {
public:
    explicit PreviewTransitionActivity(Session* s) : SetupActivity(s) {}

    void onContentAvailable() override {
        SetupActivity::onContentAvailable();
        advance_timer_.setEndCallback([this](bool) { go_next(); });
        advance_timer_.start(4000);
    }

private:
    brls::Timer advance_timer_;
};

static void run_setup_welcome(Session& session) {
    brls::Application::pushActivity(new SetupActivity(&session));
}

static void run_setup_field(Session& session) {
    auto* act = new PreviewSetupActivity(&session);
    act->preset_step1();
    brls::Application::pushActivity(act);
}

static void run_creating_vault(Session& session) {
    auto* act = new PreviewSetupActivity(&session);
    act->preset_creating_vault();
    brls::Application::pushActivity(act);
}

static void run_unlock(Session& session) {
    brls::Application::pushActivity(new UnlockActivity(&session, nullptr, 0));
}

static void run_recovery(Session& session) {
    static const std::string hex =
        "DEAD001100000001DEAD002200000002"
        "DEAD003300000003DEAD004400000004";
    brls::Application::pushActivity(
        new RecoveryKeyActivity(&session, hex, "/preview/recovery.txt"));
}

static void run_no_internet(Session&) {
    brls::Application::pushActivity(new NoInternetActivity(NoInternetReason::NoNetwork));
}

// With PREVIEW_SLOW_TRANSITION the animation lasts 2 s, leaving time for mid-animation frames.
static void run_transition(Session& session) {
    brls::Application::pushActivity(new PreviewTransitionActivity(&session));
}

static void run_loading(Session& session) {
    auto worker = []() -> LoadingActivity::LoadResult {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return {true, "", {}};
    };
    brls::Application::pushActivity(new LoadingActivity(&session, worker));
}

static PreviewModeReg reg_setup_welcome("setup-welcome", &run_setup_welcome);
static PreviewModeReg reg_setup_field("setup-field", &run_setup_field);
static PreviewModeReg reg_creating_vault("creating-vault", &run_creating_vault);
static PreviewModeReg reg_unlock("unlock", &run_unlock);
static PreviewModeReg reg_recovery("recovery", &run_recovery);
static PreviewModeReg reg_no_internet("no-internet", &run_no_internet);
static PreviewModeReg reg_transition("transition", &run_transition);
static PreviewModeReg reg_loading("loading", &run_loading);
