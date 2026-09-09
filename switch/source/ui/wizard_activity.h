#pragma once
#include <borealis.hpp>
#include "wizard.h"
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

    // Defers a rebuild by one borealis frame so that action callbacks
    // (BUTTON_A on the focused Button) return before rebuild() deletes
    // that Button — avoiding the use-after-free Data Abort on hardware.
    class RefreshPump : public brls::RepeatingTask {
      public:
        explicit RefreshPump(WizardActivity* owner)
            : brls::RepeatingTask(16), owner_(owner) {}
        void run() override {
            if (owner_->refresh_pending_) {
                owner_->refresh_pending_ = false;
                owner_->refresh();
            }
        }
      private:
        WizardActivity* owner_;
    };

    bool         refresh_pending_ = false;
    RefreshPump* refresh_pump_    = nullptr;
    bool         step_changed_    = false;
    bool         go_forward_      = true;
};
