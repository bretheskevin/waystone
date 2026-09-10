#include "wizard.h"
#include "theme.h"
#include "session.h"
#include <cstring>

Wizard::Wizard(std::vector<WizardStepDef> steps, size_t num_values)
    : steps_(steps), values_(num_values), current_step_(0), cursor_(0), has_finish_(false) {
    finish_rect_ = Rect();
    recompute_layout(false);
}
Wizard::~Wizard() { zeroize_secrets(); }
void Wizard::zeroize_secrets() {
    for (size_t s=0;s<steps_.size();s++)
        for (size_t f=0;f<steps_[s].fields.size();f++)
            if (steps_[s].fields[f].is_secret) zeroize_string(values_[steps_[s].fields[f].value_index]);
}
void Wizard::reload_steps(std::vector<WizardStepDef> new_steps) {
    steps_ = new_steps;
    if (current_step_ >= steps_.size()) current_step_ = steps_.size()-1;
    cursor_ = 0; recompute_layout(has_finish_);
}
size_t Wizard::interactive_count() const {
    size_t count = steps_[current_step_].fields.size();
    if (has_finish_) count += 1;
    return count;
}
void Wizard::recompute_layout(bool has_finish) {
    has_finish_ = has_finish; field_rects_.clear();
    const WizardStepDef& step = steps_[current_step_];
    float x = SP_XL; float w = (float)SCREEN_BOT_W - 2*SP_XL;
    float row_h = SP_2XL + SP_MD; float y = 44.0f;
    for (size_t i=0;i<step.fields.size();i++) { Rect r={x,y,w,row_h}; field_rects_.push_back(r); y += row_h + SP_SM; }
    if (has_finish) { float btn_h=28.0f, btn_w=140.0f, btn_x=((float)SCREEN_BOT_W-btn_w)/2.0f; finish_rect_=Rect{btn_x,y+SP_SM,btn_w,btn_h}; }
    if (cursor_ >= interactive_count() && interactive_count() > 0) cursor_ = interactive_count()-1;
}
void Wizard::go_next() {
    if (current_step_+1 < steps_.size()) { current_step_++; cursor_=0; error_.clear(); recompute_layout(current_step_==steps_.size()-1); }
}
void Wizard::go_back() {
    if (current_step_ > 0) { current_step_--; cursor_=0; error_.clear(); recompute_layout(false); }
}
void Wizard::draw_top(C3D_RenderTarget* target, C2D_TextBuf buf, const char* screen_title) {
    (void)target;
    draw_text_centered(buf, 0, 40.0f, 0.5f, TEXT_2XL, CLR_WHITE, screen_title, (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, "Waystone", (float)SCREEN_TOP_W);
    if (!error_.empty())
        draw_text_centered(buf, 0, (float)SCREEN_TOP_H-40.0f, 0.5f, TEXT_BASE, CLR_ERROR, error_.c_str(), (float)SCREEN_TOP_W);
    else if (!status_.empty())
        draw_text_centered(buf, 0, (float)SCREEN_TOP_H-40.0f, 0.5f, TEXT_BASE, CLR_SYNC, status_.c_str(), (float)SCREEN_TOP_W);
}
void Wizard::draw_bottom(C3D_RenderTarget* target, C2D_TextBuf buf, const char* finish_label) {
    (void)target;
    bool new_finish = (finish_label != 0 && finish_label[0] != '\0');
    if (new_finish != has_finish_) recompute_layout(new_finish);
    const WizardStepDef& step = steps_[current_step_];
    if (steps_.size() > 1) draw_step_dots(buf, (float)SCREEN_BOT_W/2.0f, SP_MD, current_step_, steps_.size());
    draw_text_centered(buf, 0, 20.0f, 0.5f, TEXT_LG, CLR_TEXT, step.title.c_str(), (float)SCREEN_BOT_W);
    if (step.fields.empty() && !step.hint.empty()) {
        draw_text_centered(buf, 0, (float)SCREEN_BOT_H-60.0f, 0.5f, TEXT_BASE, CLR_TEXT_HINT, step.hint.c_str(), (float)SCREEN_BOT_W);
        draw_footer_hint(buf, "R: Next"); return;
    }
    for (size_t i=0;i<step.fields.size();i++) {
        const WizardFieldDef& f = step.fields[i]; bool focused=(cursor_==i);
        draw_text_field_row(buf, field_rects_[i].x, field_rects_[i].y, field_rects_[i].w, f.label.c_str(),
                            values_[f.value_index].c_str(), f.is_secret, focused, f.placeholder.c_str());
    }
    if (has_finish_) {
        bool focused=(cursor_==step.fields.size());
        draw_button(buf, finish_rect_.x, finish_rect_.y, finish_rect_.w, finish_rect_.h, finish_label, ButtonStyle::PRIMARY, focused);
    }
    const char* hint = "A: Edit  R: Next  L: Back";
    if (current_step_==0) hint = "A: Edit  R: Next";
    draw_footer_hint(buf, hint);
}
void Wizard::edit_field_at_cursor() {
    const WizardStepDef& step = steps_[current_step_];
    if (cursor_ >= step.fields.size()) return;
    const WizardFieldDef& f = step.fields[cursor_];
    std::string result = swkbd_prompt(f.label.c_str(), f.is_secret, values_[f.value_index]);
    if (!result.empty() || !f.is_secret) {
        if (f.is_secret) zeroize_string(values_[f.value_index]);
        values_[f.value_index] = result;
    }
    zeroize_string(result);
}
int Wizard::handle_input(u32 kDown, touchPosition touch) {
    const WizardStepDef& step = steps_[current_step_];
    size_t count = interactive_count();
    if (kDown & KEY_DUP)   { if (count>0) cursor_=(cursor_==0)?count-1:cursor_-1; }
    if (kDown & KEY_DDOWN) { if (count>0) cursor_=(cursor_+1)%count; }
    if (kDown & KEY_A) {
        if (cursor_ < step.fields.size()) { edit_field_at_cursor(); return 1; }
        else if (has_finish_) return 2;
    }
    if (kDown & KEY_R) return 3;
    if ((kDown & KEY_L) || (kDown & KEY_B)) return 4;
    if (touch.px != 0 || touch.py != 0) {
        float tx=(float)touch.px, ty=(float)touch.py;
        for (size_t i=0;i<field_rects_.size();i++) if (field_rects_[i].contains(tx,ty)) { cursor_=i; edit_field_at_cursor(); return 1; }
        if (has_finish_ && finish_rect_.contains(tx,ty)) { cursor_=step.fields.size(); return 2; }
    }
    return 0;
}
