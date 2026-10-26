#include "theme_tint.h"
#include <borealis.hpp>
#include <cstdio>
#include "kit/ws_nvg.h"
#include "kit/ws_style.h"

void apply_waystone_tint() {
    brls::Theme light = brls::getLightTheme();
    light.addColor("brls/sidebar/active_item",               ws_nvg(WS_C_ACCENT));
    light.addColor("brls/button/primary_enabled_background", nvgRGB(0x4F, 0x46, 0xE5)); // indigo-600

    brls::Theme dark = brls::getDarkTheme();
    dark.addColor("brls/sidebar/active_item",                ws_nvg(WS_C_ACCENT));
    dark.addColor("brls/button/primary_enabled_background",  ws_nvg(WS_C_ACCENT));
    printf("[ui] theme tint applied (stock highlight, indigo accent)\n");
}
