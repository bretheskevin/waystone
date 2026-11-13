#pragma once
// Shared preview fixtures + base screen for kit preview modes: a ShellFrame with placeholder
// content and a delayed callback (overlays / toasts must start after the base activity is up).
#include <borealis.hpp>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "keymap_switch.h"
#include "session.h"
#include "setup_activity.h"
#include "sync_controller.h"
#include "kit/shell_frame.h"
#include "kit/ws_nvg.h"
#include "kit/ws_style.h"

static const char* const FIXTURE_ICON = "./resources/placeholder_icon.jpg";
// Delay before a mode pushes its overlay / drives input, once the base activity is up.
static constexpr int PREVIEW_READY_DELAY_MS = 1000;

// Stub sync state (preview/stubs/sync_controller_stub.cpp).
extern bool g_preview_sync_running;
extern bool g_preview_sync_done;

inline void preview_press(brls::ControllerButton b) {
    printf("[ui] preview: press button %d\n", (int)b);
    brls::Application::onControllerButtonPressed(b, false);
}

// Unencrypted-server keys.json fixture for UnlockActivity (plain login).
inline const uint8_t* preview_plain_keys(size_t* len) {
    static const char keys[] = "{\"encryption\":\"none\",\"version\":1}";
    *len = sizeof(keys) - 1;
    return reinterpret_cast<const uint8_t*>(keys);
}

// Leaked self-firing timer: presses `seq` once `delay_ms` has elapsed.
inline void preview_press_after(int delay_ms, std::vector<brls::ControllerButton> seq) {
    auto* timer = new brls::Timer();
    timer->setEndCallback([seq](bool) {
        for (brls::ControllerButton b : seq) preview_press(b);
    });
    timer->start(delay_ms);
}

inline void preview_seed_session(Session& session) {
    session.config.server_url = "https://dav.example.com";
    session.config.username   = "waystone";
    session.device_id         = "switch-7f3a91c2";
}

// SetupActivity opened on the WebDAV Login step; server + username come from the session config
// (preview_seed_session), as after Forget server.
class PreviewLoginSetupActivity : public SetupActivity {
  public:
    explicit PreviewLoginSetupActivity(Session* s) : SetupActivity(s) {}

    // Call BEFORE pushActivity(): borealis calls onContentAvailable() synchronously inside it.
    void preset_login(const char* password = "") {
        values_[FIELD_PASSWORD] = password;
        current_step_           = STEP_LOGIN;
    }
};

// Stubbed controller (preview/stubs/sync_controller_stub.cpp): fixture titles, canned progress.
inline SyncController* preview_sync_controller(const WaystoneShellConfig* cfg) {
    return new SyncController(nullptr, AccountUid{}, "", nullptr, {}, cfg);
}

class PreviewShellActivity : public brls::Activity {
  public:
    PreviewShellActivity(int delay_ms, std::function<void()> on_ready)
        : delay_ms_(delay_ms), on_ready_(std::move(on_ready)) {}

    brls::View* createContentView() override {
        frame_ = new ShellFrame("Waystone");
        frame_->setStatus("Idle \xc2\xb7 10 games");
        frame_->setContent(makeContent());
        frame_->setGuide(guide());
        return frame_;
    }

    void onContentAvailable() override {
        frame_->guide()->slideIn();
        if (!on_ready_) return;
        timer_.setEndCallback([this](bool) { on_ready_(); });
        timer_.start(delay_ms_);
    }

  protected:
    virtual brls::View* makeContent() {
        auto* box = new brls::Box(brls::Axis::COLUMN);
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);
        auto* label = new brls::Label();
        label->setText("Content area (y 89 \xe2\x86\x92 647)");
        label->setFontSize(22.0f);
        label->setTextColor(ws_nvg(WS_C_MUTED));
        box->addView(label);
        return box;
    }

    virtual std::string guide() const {
        return ws_hint_bar({WsAction::OpenGame, WsAction::SyncAll, WsAction::Quit});
    }

  private:
    int                   delay_ms_;
    std::function<void()> on_ready_;
    ShellFrame*           frame_ = nullptr;
    brls::Timer           timer_;
};
