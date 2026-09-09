#pragma once
#include <borealis.hpp>
#include <string>
#include <vector>

struct WizardStepDef {
    std::string label;
    std::string hint;
    bool is_secret;
    std::string placeholder;
};

// Builds and manages the wizard step UI inside a parent Box.
// Call rebuild() whenever current_step or field values change.
class WizardRenderer {
  public:
    WizardRenderer(brls::Box* parent, const std::vector<WizardStepDef>& steps);

    // Rebuild the parent's children for the given step.
    // values: current field values, indexed same as steps.
    // error: inline error string (empty = no error).
    void rebuild(size_t current_step,
                 const std::vector<std::string>& values,
                 const std::string& error);

  private:
    brls::Box* parent_;
    std::vector<WizardStepDef> steps_;

    void clear_parent();
    brls::Box* build_progress_dots(size_t current, size_t total);
};
