#pragma once
#include "screen.h"
#include "widgets.h"
#include "swkbd_util.h"
#include <string>
#include <vector>

struct WizardFieldDef {
    size_t value_index; std::string label; bool is_secret;
    std::string placeholder; std::string hint;
};
struct WizardStepDef {
    std::string title; std::string hint; std::vector<WizardFieldDef> fields;
};

class Wizard {
public:
    Wizard(std::vector<WizardStepDef> steps, size_t num_values);
    ~Wizard();
    void draw_top(C3D_RenderTarget* target, C2D_TextBuf buf, const char* screen_title);
    void draw_bottom(C3D_RenderTarget* target, C2D_TextBuf buf, const char* finish_label);
    int handle_input(u32 kDown, touchPosition touch);
    size_t current_step() const { return current_step_; }
    size_t step_count() const { return steps_.size(); }
    bool is_last_step() const { return current_step_ == steps_.size() - 1; }
    std::string& value(size_t i) { return values_[i]; }
    const std::string& value(size_t i) const { return values_[i]; }
    void set_error(const std::string& err) { error_ = err; }
    void set_status(const std::string& status) { status_ = status; }
    const std::string& error() const { return error_; }
    void clear_error() { error_.clear(); }
    void go_next();
    void go_back();
    void zeroize_secrets();
    void reload_steps(std::vector<WizardStepDef> new_steps);
private:
    std::vector<WizardStepDef> steps_;
    std::vector<std::string> values_;
    std::string error_; std::string status_;
    size_t current_step_; size_t cursor_;
    std::vector<Rect> field_rects_; Rect finish_rect_; bool has_finish_;
    void recompute_layout(bool has_finish);
    size_t interactive_count() const;
    void edit_field_at_cursor();
};
