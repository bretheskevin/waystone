#pragma once
#include <functional>
#include <memory>
#include <string>
#include "update_controller.h"

// Shared "Check for updates" flow (Settings, Unlock, Setup welcome): check -> Install dialog ->
// download -> Restart dialog. Status text goes to the sink; the owner calls tick() every frame.
class UpdateFlow {
  public:
    UpdateFlow(std::function<void(const std::string&)> status_sink, const char* origin);
    ~UpdateFlow();
    UpdateFlow(const UpdateFlow&)            = delete;
    UpdateFlow& operator=(const UpdateFlow&) = delete;

    void start_check();
    void tick();
    bool busy() const;

  private:
    enum class Offer { None, Install, Restart };

    void offer_install();
    void offer_restart();
    void start_install();
    void restart_now();
    void set_status(const std::string& text);

    std::function<void(const std::string&)> sink_;
    const char*       origin_;
    UpdateController* updater_         = nullptr;
    bool              install_started_ = false;
    bool              dialog_open_     = false;
    // Dialogs open one tick after the status is set, so a wizard owner can redraw it first.
    Offer             offer_           = Offer::None;
    UpdatePhase       last_phase_      = UpdatePhase::Idle;
    int               last_pct_        = -1;
    std::string       pending_ver_;

    // Dialog callbacks run after the dialog is deleted; they bail out once the owner is gone.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};
