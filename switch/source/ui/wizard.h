#pragma once
#include <borealis.hpp>
#include <functional>
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
    // edit_fn: called when the user activates the value button (may be empty).
    void rebuild(size_t current_step,
                 const std::vector<std::string>& values,
                 const std::string& error,
                 const std::function<void()>& edit_fn = {});

  private:
    brls::Box* parent_;
    std::vector<WizardStepDef> steps_;

    void clear_parent();
    brls::Box* build_progress_dots(size_t current, size_t total);
};
