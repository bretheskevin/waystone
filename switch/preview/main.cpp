/*
 * Waystone preview harness — renders onboarding wizard screens on desktop.
 *
 * Usage: waystone_preview <mode>
 *   setup-welcome  — SetupActivity at step 0 (Welcome)
 *   setup-field    — SetupActivity at step 1 (Server URL, pre-filled)
 *   unlock         — UnlockActivity at step 0 (Vault Passphrase)
 *   recovery       — RecoveryKeyActivity with canned recovery hex
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
        values_[1]    = "https://dav.example.com";
        current_step_ = 1;
        error_.clear();
        // refresh() will be called by onContentAvailable() inside pushActivity().
    }
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "setup-welcome";

    // Apply Waystone brand tint before Application::init() so that the
    // theme override takes effect immediately.
    apply_waystone_tint();

    brls::Logger::setLogLevel(brls::LogLevel::WARNING);

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialise borealis");
        return 1;
    }

    // Window title visible in the Xvfb WM_NAME (helpful for debugging).
    brls::Application::createWindow("Waystone Preview");

    // Session is never used to make real network calls in this build;
    // vault_helpers_stub and ws_ffi_stub provide no-op implementations.
    static Session session;

    if (strcmp(mode, "setup-welcome") == 0) {
        brls::Application::pushActivity(new SetupActivity(&session));

    } else if (strcmp(mode, "setup-field") == 0) {
        // IMPORTANT: preset before pushActivity — borealis calls
        // onContentAvailable() synchronously inside pushActivity().
        auto* act = new PreviewSetupActivity(&session);
        act->preset_step1();
        brls::Application::pushActivity(act);

    } else if (strcmp(mode, "unlock") == 0) {
        // keys_data=nullptr, keys_len=0 — the unlock wizard doesn't try to
        // decrypt until the user presses "Unlock" (which we never trigger).
        brls::Application::pushActivity(
            new UnlockActivity(&session, nullptr, 0));

    } else if (strcmp(mode, "recovery") == 0) {
        static const std::string hex =
            "DEAD001100000001DEAD002200000002"
            "DEAD003300000003DEAD004400000004";
        brls::Application::pushActivity(
            new RecoveryKeyActivity(&session, hex, "/preview/recovery.txt"));

    } else {
        brls::Logger::error("Unknown mode: %s", mode);
        return 1;
    }

    // Run until the process is killed by the screenshot harness.
    while (brls::Application::mainLoop())
        ;

    return 0;
}
