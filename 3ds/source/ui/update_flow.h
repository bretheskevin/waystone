#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <string>
#include "update_worker.h"

class Wizard;

// Check -> confirm -> download/install -> restart prompt, shared by Settings, Unlock and Setup.
class UpdateFlow {
public:
    UpdateFlow();
    ~UpdateFlow();
    void start_check(const char* origin);
    // Returns true when status() changed since the previous poll() (input included).
    bool poll();
    bool modal() const { return confirm_update_ || restart_prompt_; }
    bool busy() const { return (worker_ && worker_->is_running()) || modal(); }
    // Consumes kDown while modal(); returns false (input untouched) otherwise.
    bool handle_input(u32 kDown);
    // Draws the confirm / restart banner and its footer hint; returns false when not modal().
    bool draw_banner(C2D_TextBuf buf) const;
    const std::string& status() const { return status_text_; }
    u32 status_color() const;
    // Hands the worker to App::defer_reap; call before set_screen / pop_screen.
    void release();
    // Wizard screens (Unlock, Setup): poll() and copy a changed, non-empty status into the wizard
    // status line unless a screen job owns it (muted).
    void poll_into(Wizard* wizard, bool muted);
    // Wizard screens: modal input first; then, while check_allowed, CheckUpdates clears the wizard
    // error and starts a check. Returns true when kDown was consumed.
    bool handle_wizard_input(u32 kDown, Wizard* wizard, const char* origin, bool check_allowed);
private:
    UpdateWorker* worker_;
    UpdatePhase phase_;
    UpdatePhase last_phase_;
    bool confirm_update_;
    bool update_cancelled_;  // keeps the "postponed" text out of the success color
    bool install_started_;   // distinguishes install-Done from check-Done
    bool restart_prompt_;    // CIA installed: "Restart now / Later"
    bool status_changed_;
    std::string pending_ver_;
    std::string pending_url_;
    std::string status_text_;
    void set_status(const std::string& s);
    UpdateFlow(const UpdateFlow&);
    UpdateFlow& operator=(const UpdateFlow&);
};
