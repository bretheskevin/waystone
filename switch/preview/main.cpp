/*
 * Waystone preview harness — renders onboarding wizard screens on desktop.
 *
 * Usage: waystone_preview <mode>
 *   setup-welcome   — SetupActivity at step 0 (Welcome)
 *   setup-field     — SetupActivity at step 1 (Server URL, pre-filled)
 *   creating-vault  — SetupActivity at step 3, "Creating vault…" progress state
 *   unlock          — UnlockActivity at step 0 (Vault Passphrase)
 *   recovery        — RecoveryKeyActivity with canned recovery hex
 *   no-internet     — NoInternetActivity (no-network reason)
 *   loading         — LoadingActivity spinner (3-second simulated worker)
 *   transition      — SetupActivity auto-advancing from step 0→1 (for GIF capture)
 *   conflicts       — ConflictsActivity with 3 canned conflicts (normal mode)
 *   conflicts-confirm — ConflictsActivity with confirm banner auto-triggered
 *   history           — HistoryActivity with 3 canned history entries
 *   sync-running      — SyncActivity modal with canned mid-sync progress
 *   dashboard         — TitleListActivity (AppletFrame + icon+name rows)
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
#include "loading_activity.h"
#include "conflicts_activity.h"
#include "history_controller.h"
#include "history_activity.h"
#include "title_list_activity.h"
#include "sync_activity.h"
#include "session.h"
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

extern bool g_preview_sync_running;

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

    // Show the "Creating vault…" progress state (step 3, worker running).
    // Does NOT start a real worker thread — only presets the visual state for
    // screenshot capture.  Call BEFORE pushActivity().
    void preset_creating_vault() {
        values_[0]       = "https://dav.example.com";  // FIELD_SERVER
        values_[1]       = "waystone";                  // FIELD_USERNAME
        current_step_    = 3;
        creating_vault_  = true;
        status_          = "Creating vault\xe2\x80\xa6 please wait";
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
// Preview-only subclass of ConflictsActivity.
// Auto-triggers the confirm banner after 1 second via a timer,
// so the screenshot script can capture it without manual input.
// ---------------------------------------------------------------------------
class PreviewConflictsConfirmActivity : public ConflictsActivity {
public:
    using ConflictsActivity::ConflictsActivity;

    void onContentAvailable() override {
        ConflictsActivity::onContentAvailable();
        confirm_timer_.setEndCallback([this](bool) {
            trigger_confirm_for_preview();
        });
        confirm_timer_.start(1000);
    }

private:
    brls::Timer confirm_timer_;
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

    } else if (strcmp(mode, "creating-vault") == 0) {
        auto* act = new PreviewSetupActivity(&session);
        act->preset_creating_vault();
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

    } else if (strcmp(mode, "loading") == 0) {
        // Preview the loading spinner without a real worker
        auto worker = []() -> LoadingActivity::LoadResult {
            // Simulate a 3-second load
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return {true, "", {}};
        };
        brls::Application::pushActivity(
            new LoadingActivity(&session, worker));

    } else if (strcmp(mode, "conflicts") == 0) {
        auto* ctrl = new ConflictController(
            nullptr, AccountUid{}, "",
            WebDavCfg{nullptr, nullptr, nullptr}, {}, session.config);
        ctrl->start_scan();
        brls::Application::pushActivity(new ConflictsActivity(ctrl));

    } else if (strcmp(mode, "conflicts-confirm") == 0) {
        auto* ctrl = new ConflictController(
            nullptr, AccountUid{}, "",
            WebDavCfg{nullptr, nullptr, nullptr}, {}, session.config);
        ctrl->start_scan();
        brls::Application::pushActivity(
            new PreviewConflictsConfirmActivity(ctrl));

    } else if (strcmp(mode, "sync-running") == 0) {
        g_preview_sync_running = true;
        auto* ctrl = new SyncController(nullptr, AccountUid{}, "", WebDavCfg{nullptr, nullptr, nullptr}, {}, &session.config);
        brls::Application::pushActivity(new SyncActivity(ctrl));

    } else if (strcmp(mode, "history") == 0) {
        TitleInfo ti;
        ti.title_id = 0x0100F2C0115B6000ULL;
        ti.name = "The Legend of Zelda: TotK";
        auto* hc = new HistoryController(ti, &session);
        hc->start_scan();
        brls::Application::pushActivity(new HistoryActivity(hc));

    } else if (strcmp(mode, "dashboard") == 0) {
        // Dashboard mode: title list with fixture games from sync_controller_stub.
        // Exercises the AppletFrame header, icon+name rows, Sync button, and status line.
        auto* ctrl = new SyncController(nullptr, AccountUid{}, "",
                                        WebDavCfg{nullptr, nullptr, nullptr}, {}, &session.config);
        brls::Application::pushActivity(new TitleListActivity(ctrl, &session));

    } else {
        brls::Logger::error("Unknown mode: %s", mode);
        return 1;
    }

    while (brls::Application::mainLoop())
        ;

    return 0;
}
