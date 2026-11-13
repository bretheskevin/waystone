#pragma once
#include <borealis.hpp>
#include "wizard.h"
#include "deferred_refresh_pump.h"
#include "setup_model.h"
#include "keymap.h"
#include <functional>
#include <string>
#include <vector>

class UpdateFlow;

// Base class for wizard-style activities sharing the step-navigation
// state machine: members, view construction, action registration, and
// step traversal.  Subclasses supply step definitions, validation,
// finish behaviour, and (optionally) extra button actions.
class WizardActivity : public brls::Activity {
  public:
    brls::View* createContentView() override;
    void        onContentAvailable() override;

  protected:
    explicit WizardActivity(size_t num_values);
    ~WizardActivity() override;

    brls::Box*               content_box_  = nullptr;
    brls::Label*             hint_label_   = nullptr;
    WizardRenderer*          renderer_     = nullptr;
    size_t                   current_step_ = 0;
    std::vector<std::string> values_;
    std::string              error_;
    std::string              status_;

    // Optional poll callback invoked on every pump tick (main thread).
    // Set by a subclass to marshal a background result back to the UI thread.
    // The pump copies the function before calling it, so the lambda may safely
    // clear poll_fn_ without destroying the currently-running instance.
    std::function<void()> poll_fn_;

    // Persistent per-frame callbacks, ticked after poll_fn_.
    // Never removed; may be added from the ctor, register_extra_actions() or an action.
    void add_ticker(std::function<void()> fn);

    // UpdateFlow status sink: shows the text in the wizard status line.
    std::function<void(const std::string&)> status_sink();
    // CheckUpdates (Select) starts flow->start_check() while allowed() holds; ticks flow every frame.
    // Call from register_extra_actions().
    void register_update_check(UpdateFlow* flow, const char* origin, std::function<bool()> allowed);

    void refresh();
    void schedule_refresh();
    void reload_steps();
    void go_next();
    void go_back();
    void zeroize_secrets();
    void edit_field(const WizardFieldDef& f);

    // Passphrase / recovery-key secret field shared by Unlock and Setup (join).
    static WizardFieldDef vault_secret_field(size_t value_index, bool recovery);
    // Y flips *recovery_mode while active() holds: wipes the field and reloads the steps.
    void register_recovery_toggle(bool* recovery_mode, size_t value_index,
                                  std::function<bool()> active);
    void toggle_recovery(bool* recovery_mode, size_t value_index);

    virtual std::vector<WizardStepDef> get_steps()      const = 0;
    virtual std::string                finish_label()    const = 0;
    virtual std::string                wizard_title()    const { return ""; }
    virtual bool                       validate_step(size_t step);
    virtual void                       on_finish()             = 0;
    virtual void                       register_extra_actions();
    // True while a background worker owns the wizard: Next/Back/field edits are ignored.
    virtual bool                       nav_locked()      const { return false; }
    // Extra footer hints, shown before Quit; re-read on every refresh (may depend on current_step_).
    virtual std::vector<WsAction>      extra_hint_actions() const { return {}; }

  private:
    std::vector<WizardStepDef> steps_;
    bool step_changed_ = false;
    bool go_forward_   = true;
    std::vector<std::function<void()>> tickers_;

    void pump_refresh();
    void pump_tick();

    // Deferred pump: on_refresh rebuilds the view; on_tick forwards to poll_fn_
    // (copy-before-call so the lambda can safely clear poll_fn_ without UAF) and tickers_.
    DeferredRefreshPump pump_{[this] { pump_refresh(); }, [this] { pump_tick(); }};
};
