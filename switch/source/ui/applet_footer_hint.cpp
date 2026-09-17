#include "applet_footer_hint.h"

// Muted grey that matches the wizard's footer hints.
static NVGcolor footer_hint_color() { return nvgRGB(0x71, 0x71, 0x7A); }

brls::Label* set_footer_hint(brls::AppletFrame* frame, const std::string& text) {
    auto& children = frame->getChildren();
    if (children.size() < 3) return nullptr;

    auto* footer = static_cast<brls::Box*>(children[2]);
    auto& fc = footer->getChildren();
    while (!fc.empty()) footer->removeView(fc.front());

    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(18.0f);
    label->setTextColor(footer_hint_color());
    footer->addView(label);
    return label;
}
