#pragma once
#include <borealis.hpp>
#include <cstdint>
#include <string>

inline NVGcolor ws_nvg(uint32_t rgba) {
    return nvgRGBA(static_cast<unsigned char>(rgba >> 24), static_cast<unsigned char>(rgba >> 16),
                   static_cast<unsigned char>(rgba >> 8), static_cast<unsigned char>(rgba));
}

// Regular font (with the Switch + Material icon fallbacks) at the given size/color/alignment.
inline void ws_nvg_font(NVGcontext* vg, float size, uint32_t rgba, int align) {
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgFontSize(vg, size);
    nvgFillColor(vg, ws_nvg(rgba));
    nvgTextAlign(vg, align);
}

inline float ws_nvg_text_width(NVGcontext* vg, float size, const std::string& text) {
    if (!vg || text.empty()) return 0.0f;
    nvgSave(vg);
    ws_nvg_font(vg, size, 0xFFFFFFFF, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float bounds[4];
    float advance = nvgTextBounds(vg, 0, 0, text.c_str(), nullptr, bounds);
    nvgRestore(vg);
    return advance;
}

inline void ws_nvg_fill_rect(NVGcontext* vg, float x, float y, float w, float h, float radius, uint32_t rgba) {
    nvgBeginPath(vg);
    if (radius > 0.0f) nvgRoundedRect(vg, x, y, w, h, radius);
    else nvgRect(vg, x, y, w, h);
    nvgFillColor(vg, ws_nvg(rgba));
    nvgFill(vg);
}
