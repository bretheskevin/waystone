#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

struct WizardFieldDef {
    size_t      value_index;
    std::string label;
    bool        is_secret;
    std::string placeholder;
    std::string hint;
};

struct WizardStepDef {
    std::string               title;
    std::string               hint;    // shown on welcome (0-field) steps
    std::vector<WizardFieldDef> fields;
};

enum class WizardTransition { NONE, FORWARD, BACK };

// Builds and manages the wizard step UI inside a parent Box.
// Call rebuild() whenever current_step or field values change.
class WizardRenderer {
  public:
    WizardRenderer(brls::Box* parent, const std::vector<WizardStepDef>& steps);
    ~WizardRenderer();

    // Rebuild the parent's children for the given step.
    // transition=NONE → instant swap (first render, error re-render).
    // transition=FORWARD/BACK → slide+fade animation.
    void rebuild(size_t current_step,
                 const std::vector<std::string>& values,
                 const std::string& error,
                 const std::string& status,
                 const std::function<void(const WizardFieldDef&)>& edit_cb = {},
                 const std::string& finish_label = {},
                 const std::function<void()>& finish_cb = {},
                 WizardTransition transition = WizardTransition::NONE);

  private:
    brls::Box* parent_;
    std::vector<WizardStepDef> steps_;

    brls::Box* dots_row_    = nullptr;
    brls::Box* step_slot_   = nullptr;
    brls::Box* active_box_  = nullptr;

    bool       in_transition_ = false;
    brls::Box* outgoing_box_  = nullptr;
    brls::Animatable out_tx_{0.0f};
    brls::Animatable in_tx_{0.0f};

    brls::Box* make_step_content(size_t step,
                                  const std::vector<std::string>& values,
                                  const std::string& error,
                                  const std::string& status,
                                  const std::function<void(const WizardFieldDef&)>& edit_cb,
                                  const std::string& finish_label,
                                  const std::function<void()>& finish_cb);

    brls::Box* build_dots_row(size_t current, size_t total);
    void refresh_dots(size_t current);
    void abort_transition();
    void init_layout(size_t step,
                     const std::vector<std::string>& values,
                     const std::string& error,
                     const std::string& status,
                     const std::function<void(const WizardFieldDef&)>& edit_cb,
                     const std::string& finish_label,
                     const std::function<void()>& finish_cb);
    void swap_instant(size_t step,
                      const std::vector<std::string>& values,
                      const std::string& error,
                      const std::string& status,
                      const std::function<void(const WizardFieldDef&)>& edit_cb,
                      const std::string& finish_label,
                      const std::function<void()>& finish_cb);
};
