#include "guide_pill.h"
#include "ws_nvg.h"
#include "ws_style.h"

GuidePill::GuidePill() {
    detach();
    setFocusable(false);
    place();
}

void GuidePill::setText(const std::string& text) {
    if (text == text_) return;
    text_ = text;
    place();
}

void GuidePill::place() {
    float text_w = ws_nvg_text_width(brls::Application::getNVGContext(), WS_GUIDE_TEXT_SIZE, text_);
    pill_w_ = text_.empty() ? 0.0f : text_w + WS_GUIDE_PAD_W;
    setDetachedPosition(WS_SCREEN_W - pill_w_, WS_GUIDE_Y);
    setDimensions(pill_w_, WS_GUIDE_H);
}

void GuidePill::slideIn() {
    slide_.reset(1.0f);
    slide_.addStep(0.0f, WS_ANIM_SLIDE_MS, brls::EasingFunction::quadraticOut);
    slide_.setTickCallback([this] { setTranslationX(slide_.getValue() * pill_w_); });
    setTranslationX(pill_w_);
    slide_.start();
}

void GuidePill::draw(NVGcontext* vg, float x, float y, float, float, brls::Style, brls::FrameContext*) {
    if (text_.empty()) return;
    nvgSave(vg);
    nvgGlobalAlpha(vg, getAlpha());
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, x, y, pill_w_, WS_GUIDE_H, WS_GUIDE_RADIUS, 0.0f, 0.0f, WS_GUIDE_RADIUS);
    nvgFillColor(vg, ws_nvg(WS_C_GUIDE));
    nvgFill(vg);
    ws_nvg_font(vg, WS_GUIDE_TEXT_SIZE, WS_C_TEXT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, x + WS_GUIDE_INSET, y + WS_GUIDE_H / 2.0f, text_.c_str(), nullptr);
    nvgRestore(vg);
}
