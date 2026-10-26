#include "shell_frame.h"
#include "version.h"
#include "ws_nvg.h"
#include "ws_style.h"

ShellFrame::ShellFrame(const std::string& title) : title_(title) {
    setBackgroundColor(ws_nvg(WS_C_BG));
    guide_ = new GuidePill();
    addView(guide_);
}

void ShellFrame::setContent(brls::View* content) {
    if (content_) removeView(content_);
    content_ = content;
    if (!content_) return;
    content_->setPositionType(brls::PositionType::ABSOLUTE);
    content_->setPositionTop(WS_CONTENT_Y);
    content_->setPositionLeft(0.0f);
    content_->setDimensions(WS_SCREEN_W, WS_CONTENT_H);
    addView(content_, 0);
}

void ShellFrame::setStatus(const std::string& status) { status_ = status; }

void ShellFrame::setGuide(const std::string& text) { guide_->setText(text); }

void ShellFrame::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                      brls::FrameContext* ctx) {
    nvgSave(vg);
    nvgGlobalAlpha(vg, getAlpha());

    ws_nvg_fill_rect(vg, WS_MARK_X, WS_MARK_Y, WS_MARK_SIZE, WS_MARK_SIZE, WS_MARK_RADIUS, WS_C_ACCENT);
    ws_nvg_font(vg, WS_MARK_GLYPH_SIZE, WS_C_TEXT, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, WS_MARK_X + WS_MARK_SIZE / 2.0f, WS_MARK_Y + WS_MARK_SIZE / 2.0f, WS_ICON_SYNC, nullptr);

    ws_nvg_font(vg, WS_TITLE_SIZE, WS_C_TEXT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, WS_TITLE_X, WS_HEADER_H / 2.0f, title_.c_str(), nullptr);

    if (!status_.empty()) {
        ws_nvg_font(vg, WS_STATUS_SIZE, WS_C_MUTED, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, WS_STATUS_RIGHT_X, WS_HEADER_H / 2.0f, status_.c_str(), nullptr);
    }

    float rule_w = WS_RULE_X1 - WS_RULE_X0;
    ws_nvg_fill_rect(vg, WS_RULE_X0, WS_RULE_TOP_Y, rule_w, WS_RULE_W, 0.0f, WS_C_RULE);
    ws_nvg_fill_rect(vg, WS_RULE_X0, WS_RULE_BOTTOM_Y, rule_w, WS_RULE_W, 0.0f, WS_C_RULE);

    ws_nvg_font(vg, WS_VERSION_SIZE, WS_C_MUTED, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgText(vg, WS_VERSION_X, WS_VERSION_BASELINE, "v" WS_APP_VERSION, nullptr);

    nvgRestore(vg);

    brls::Box::draw(vg, x, y, width, height, style, ctx);
    toast_host_.draw(vg);
}
