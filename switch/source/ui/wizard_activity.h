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
    explicit WizardActivity(size_t num_steps);
    ~WizardActivity() override;

    brls::Box*               content_box_  = nullptr;
    WizardRenderer*          renderer_     = nullptr;
    size_t                   current_step_ = 0;
    std::vector<std::string> values_;
    std::string              error_;

    void refresh();
    void go_next();
    void go_back();
    void zeroize_secrets();

    virtual std::vector<WizardStepDef> get_steps()      const = 0;
    virtual std::string                finish_label()    const = 0;
    virtual bool                       validate_step(size_t step);
    virtual void                       on_finish()             = 0;
    virtual void                       register_extra_actions();
    virtual void                       edit_current_field()    = 0;
};
