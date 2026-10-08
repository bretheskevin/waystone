#include "confirm_banner.h"
#include "keymap_switch.h"

static brls::Label* banner_line(const std::string& text, float size, NVGcolor color) {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(size);
    label->setTextColor(color);
    label->setSingleLine(true);
    return label;
}

brls::Box* make_confirm_banner(const std::string& title, const std::string& detail) {
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPadding(12.0f);
    box->setMargins(12.0f, 0.0f, 12.0f, 0.0f);
    box->setBackgroundColor(nvgRGBA(0xFF, 0xAA, 0x00, 0xFF));
    box->addView(banner_line(title, 22.0f, nvgRGB(0x1A, 0x1A, 0x1A)));
    if (!detail.empty())
        box->addView(banner_line(detail, 18.0f, nvgRGB(0x33, 0x33, 0x33)));
    static const WsAction hint[] = { WsAction::Confirm, WsAction::Cancel };
    box->addView(banner_line(ws_hint_bar(hint), 18.0f, nvgRGB(0x33, 0x33, 0x33)));
    return box;
}
