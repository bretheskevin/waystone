#pragma once
#include <borealis.hpp>
#include "wizard.h"
#include "deferred_refresh_pump.h"
#include <functional>
#include <string>
#include <vector>

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

    void refresh();
    void schedule_refresh();
    void reload_steps();
    void go_next();
    void go_back();
    void zeroize_secrets();
    void edit_field(const WizardFieldDef& f);

    virtual std::vector<WizardStepDef> get_steps()      const = 0;
    virtual std::string                finish_label()    const = 0;
    virtual std::string                wizard_title()    const { return ""; }
    virtual bool                       validate_step(size_t step);
    virtual void                       on_finish()             = 0;
    virtual void                       register_extra_actions();

  private:
    std::vector<WizardStepDef> steps_;
    bool step_changed_ = false;
    bool go_forward_   = true;

    // Deferred pump: on_refresh rebuilds the view; on_tick forwards to poll_fn_
    // (copy-before-call so the lambda can safely clear poll_fn_ without UAF).
    DeferredRefreshPump pump_{
        [this]{ refresh(); },
        [this]{ if (poll_fn_) { auto fn = poll_fn_; fn(); } }};
};
