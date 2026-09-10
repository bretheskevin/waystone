/*
 * Waystone preview harness — renders onboarding wizard screens on desktop.
 *
 * Usage: waystone_preview <mode>
 *   setup-welcome  — SetupActivity at step 0 (Welcome)
 *   setup-field    — SetupActivity at step 1 (Server URL, pre-filled)
 *   unlock         — UnlockActivity at step 0 (Vault Passphrase)
 *   recovery       — RecoveryKeyActivity with canned recovery hex
 *   no-internet    — NoInternetActivity (no-network reason)
 *   transition     — SetupActivity auto-advancing from step 0→1 (for GIF capture)
 *
 * Set BOREALIS_THEME=DARK before running to force the dark theme.
 * The binary must be launched from switch/lib/borealis/ so that
 * ./resources/ (fonts, i18n) is found.
 */

#include <borealis.hpp>
#include "theme_tint.h"
#include "setup_activity.h"
#include "unlock_activity.h"
#include "recovery_key_activity.h"
#include "no_internet_activity.h"
#include "session.h"
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Preview-only subclass of SetupActivity.
// Exposes protected WizardActivity members to allow pre-advancing steps
// without modifying the shipped source.
// ---------------------------------------------------------------------------
class PreviewSetupActivity : public SetupActivity {
public:
    explicit PreviewSetupActivity(Session* s) : SetupActivity(s) {}

    // Call BEFORE pushActivity() — borealis calls onContentAvailable()
    // synchronously inside pushActivity(), so state must be set first.
    void preset_step1() {
        values_[0]    = "https://dav.example.com";  // FIELD_SERVER = 0
        current_step_ = 1;
        error_.clear();
    }
};

// ---------------------------------------------------------------------------
// Preview-only subclass for transition capture.
// After borealis init, installs a one-shot timer to call go_next() so the
// transition animation plays while the screenshot script captures frames.
// ---------------------------------------------------------------------------
class PreviewTransitionActivity : public SetupActivity {
public:
    explicit PreviewTransitionActivity(Session* s) : SetupActivity(s) {}

    void onContentAvailable() override {
        SetupActivity::onContentAvailable();
        advance_timer_.setEndCallback([this](bool) { go_next(); });
        advance_timer_.start(4000);  // fire 4 s after init
    }

private:
    brls::Timer advance_timer_;
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "setup-welcome";

    apply_waystone_tint();

    brls::Logger::setLogLevel(brls::LogLevel::WARNING);

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialise borealis");
        return 1;
    }

    brls::Application::createWindow("Waystone Preview");

    static Session session;

    if (strcmp(mode, "setup-welcome") == 0) {
        brls::Application::pushActivity(new SetupActivity(&session));

    } else if (strcmp(mode, "setup-field") == 0) {
        auto* act = new PreviewSetupActivity(&session);
        act->preset_step1();
        brls::Application::pushActivity(act);

    } else if (strcmp(mode, "unlock") == 0) {
        brls::Application::pushActivity(
            new UnlockActivity(&session, nullptr, 0));

    } else if (strcmp(mode, "recovery") == 0) {
        static const std::string hex =
            "DEAD001100000001DEAD002200000002"
            "DEAD003300000003DEAD004400000004";
        brls::Application::pushActivity(
            new RecoveryKeyActivity(&session, hex, "/preview/recovery.txt"));

    } else if (strcmp(mode, "no-internet") == 0) {
        brls::Application::pushActivity(
            new NoInternetActivity(NoInternetReason::NoNetwork));

    } else if (strcmp(mode, "transition") == 0) {
        // Renders step 0 (Welcome) and auto-advances to step 1 after 4 s.
        // With PREVIEW_SLOW_TRANSITION the animation lasts 2 s, giving the
        // screenshot script time to capture mid-animation frames.
        brls::Application::pushActivity(new PreviewTransitionActivity(&session));

    } else {
        brls::Logger::error("Unknown mode: %s", mode);
        return 1;
    }

    while (brls::Application::mainLoop())
        ;

    return 0;
}
