#include "theme_tint.h"
#include <borealis.hpp>

void apply_waystone_tint() {
    brls::Theme light = brls::getLightTheme();
    light.addColor("brls/highlight/color1",                  nvgRGB(0x63, 0x66, 0xF1)); // indigo-500
    light.addColor("brls/highlight/color2",                  nvgRGB(0x06, 0xB6, 0xD4)); // cyan-500
    light.addColor("brls/sidebar/active_item",               nvgRGB(0x63, 0x66, 0xF1));
    light.addColor("brls/button/primary_enabled_background", nvgRGB(0x4F, 0x46, 0xE5)); // indigo-600

    brls::Theme dark = brls::getDarkTheme();
    dark.addColor("brls/highlight/color1",                   nvgRGB(0x81, 0x8C, 0xF8)); // indigo-400
    dark.addColor("brls/highlight/color2",                   nvgRGB(0x22, 0xD3, 0xEE)); // cyan-400
    dark.addColor("brls/sidebar/active_item",                nvgRGB(0x81, 0x8C, 0xF8));
    dark.addColor("brls/button/primary_enabled_background",  nvgRGB(0x63, 0x66, 0xF1)); // indigo-500
}
