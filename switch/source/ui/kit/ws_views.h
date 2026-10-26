#pragma once
#include <borealis.hpp>
#include <cstdio>
#include <string>
#include "ws_nvg.h"
#include "ws_style.h"

// Multi-line (wrapping) label.
inline brls::Label* ws_make_text(const std::string& text, float size, uint32_t color) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(ws_nvg(color));
    return l;
}

// Single-line labels must never hold "": an empty text measure breaks the parent's Yoga layout.
inline void ws_set_label_text(brls::Label* l, const std::string& text) {
    l->setText(text.empty() ? std::string(" ") : text);
}

inline brls::Label* ws_make_label(const std::string& text, float size, uint32_t color) {
    brls::Label* l = ws_make_text(" ", size, color);
    l->setSingleLine(true);
    ws_set_label_text(l, text);
    return l;
}

inline void ws_place_absolute(brls::View* v, float left, float top) {
    v->setPositionType(brls::PositionType::ABSOLUTE);
    v->setPositionLeft(left);
    v->setPositionTop(top);
}

// Vertical accent bar marking the active/focused item; toggled with ws_show.
inline brls::Rectangle* ws_make_accent_bar(float left, float top, float width, float height) {
    auto* bar = new brls::Rectangle(ws_nvg(WS_C_ACCENT));
    bar->setWidth(width);
    bar->setHeight(height);
    ws_place_absolute(bar, left, top);
    bar->setVisibility(brls::Visibility::INVISIBLE);
    return bar;
}

inline void ws_show(brls::View* v, bool shown) {
    v->setVisibility(shown ? brls::Visibility::VISIBLE : brls::Visibility::INVISIBLE);
}

// Rounded pill/circle holding a single centered label.
inline brls::Box* ws_make_pill(brls::Label* label, uint32_t bg, float height, float radius) {
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setBackgroundColor(ws_nvg(bg));
    pill->setCornerRadius(radius);
    pill->setHeight(height);
    pill->setJustifyContent(brls::JustifyContent::CENTER);
    pill->setAlignItems(brls::AlignItems::CENTER);
    label->setHeight(height);
    label->setVerticalAlign(brls::VerticalAlign::CENTER);
    pill->addView(label);
    return pill;
}

// Loads into an Image that is already in the tree (correct Yoga constraints). false -> failed.
inline bool ws_load_image(brls::Image* img, const std::string& path) {
    if (path.empty()) return false;
    try {
        img->setImageFromFile(path);
    } catch (...) {
        printf("[ui] image load failed for '%s', caller falls back to placeholder\n", path.c_str());
        return false;
    }
    return true;
}

// Adds a size x size icon to `host` at `position` (default: last): the image at `path`, else a
// `placeholder` rect.
inline brls::View* ws_add_icon_art(brls::Box* host, const std::string& path, NVGcolor placeholder,
                                   float size, size_t position = SIZE_MAX) {
    auto add = [host, position, size](brls::View* v) {
        v->setWidth(size);
        v->setHeight(size);
        if (position < host->getChildren().size()) host->addView(v, position);
        else host->addView(v);
    };
    if (!path.empty()) {
        auto* img = new brls::Image();
        img->setScalingType(brls::ImageScalingType::STRETCH);
        add(img);
        if (ws_load_image(img, path)) return img;
        host->removeView(img);
    }
    auto* rect = new brls::Rectangle(placeholder);
    add(rect);
    return rect;
}
