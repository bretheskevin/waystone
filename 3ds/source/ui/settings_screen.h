#pragma once
#include "screen.h"
#include "session.h"
#include "update_worker.h"
#include <string>

class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(Session* session);
    ~SettingsScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    UpdateWorker* worker_;
    size_t cursor_;
    std::string status_text_;
    UpdatePhase phase_;
    UpdatePhase last_phase_;
    bool confirm_update_;   // confirm banner for "update now?"
    bool update_cancelled_; // "Update cancelled" shown: exclude it from the success color
    bool install_started_;  // distinguishes install-Done from check-Done
    std::string pending_ver_;
    std::string pending_url_;
    static const size_t NUM_ROWS = 7;
    SettingsScreen(const SettingsScreen&);
    SettingsScreen& operator=(const SettingsScreen&);
};
