#include "toast.h"
#include "toast_math.h"
#include "ws_nvg.h"
#include "ws_style.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
std::string g_text;
brls::Time  g_start_us = 0;
bool        g_active   = false;

std::vector<const WsToastHost*>& hosts() {
    static std::vector<const WsToastHost*> v;
    return v;
}
}

void ws_toast(const std::string& text) {
    printf("[ui] toast: %s\n", text.c_str());
    g_text     = text;
    g_start_us = brls::getCPUTimeUsec();
    g_active   = true;
}

void ws_toast_draw(NVGcontext* vg) {
    if (!g_active) return;
    int64_t elapsed_ms = static_cast<int64_t>((brls::getCPUTimeUsec() - g_start_us) / 1000);
    float alpha = ws_toast_alpha(elapsed_ms);
    if (alpha <= 0.0f) {
        g_active = false;
        return;
    }

    float w = ws_nvg_text_width(vg, WS_TOAST_TEXT_SIZE, g_text) + 2.0f * WS_TOAST_PAD;
    float y = WS_TOAST_BOTTOM_Y - WS_TOAST_H;

    nvgSave(vg);
    nvgGlobalAlpha(vg, alpha);
    ws_nvg_fill_rect(vg, WS_TOAST_X, y, w, WS_TOAST_H, WS_TOAST_RADIUS, WS_C_TOAST);
    ws_nvg_font(vg, WS_TOAST_TEXT_SIZE, WS_C_TEXT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, WS_TOAST_X + WS_TOAST_PAD, y + WS_TOAST_H / 2.0f, g_text.c_str(), nullptr);
    nvgRestore(vg);
}

WsToastHost::WsToastHost() { hosts().push_back(this); }

WsToastHost::~WsToastHost() {
    auto& v = hosts();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

void WsToastHost::draw(NVGcontext* vg) {
    if (!hosts().empty() && hosts().back() == this) ws_toast_draw(vg);
}
