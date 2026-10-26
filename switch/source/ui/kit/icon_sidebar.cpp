#include "icon_sidebar.h"
#include <cstdio>
#include "ws_style.h"
#include "ws_views.h"

static constexpr float GLYPH_BOX_H = WS_SIDEBAR_GLYPH_SIZE + 8.0f;
static constexpr float LABEL_BOX_H = WS_SIDEBAR_LABEL_SIZE + 6.0f;
static constexpr float GLYPH_TOP   = (WS_SIDEBAR_ITEM_H - GLYPH_BOX_H - LABEL_BOX_H) * 0.5f;

IconSidebar::IconSidebar() : brls::Box(brls::Axis::COLUMN) {
    setWidth(WS_SIDEBAR_W);
    setBackgroundColor(ws_nvg(WS_C_SIDEBAR));
    setPaddingTop(WS_SIDEBAR_PAD_TOP);
}

size_t IconSidebar::addItem(const char* glyph_utf8, const std::string& label) {
    Item it{};
    it.box = new brls::Box(brls::Axis::COLUMN);
    it.box->setWidth(WS_SIDEBAR_ITEM_W);
    it.box->setHeight(WS_SIDEBAR_ITEM_H);
    it.box->setMarginBottom(WS_SIDEBAR_GAP);
    it.box->setJustifyContent(brls::JustifyContent::CENTER);
    it.box->setAlignItems(brls::AlignItems::CENTER);
    it.box->setFocusable(true);

    auto* glyph = ws_make_label(glyph_utf8, WS_SIDEBAR_GLYPH_SIZE, WS_C_TEXT);
    glyph->setWidth(WS_SIDEBAR_ITEM_W);
    glyph->setHeight(GLYPH_BOX_H);
    glyph->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    it.box->addView(glyph);

    it.label = ws_make_label(label, WS_SIDEBAR_LABEL_SIZE, WS_C_TEXT);
    it.label->setWidth(WS_SIDEBAR_ITEM_W);
    it.label->setHeight(LABEL_BOX_H);
    it.label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    it.box->addView(it.label);

    it.bar = ws_make_accent_bar(WS_SIDEBAR_BAR_X, (WS_SIDEBAR_ITEM_H - WS_SIDEBAR_BAR_H) * 0.5f,
                                WS_SIDEBAR_BAR_W, WS_SIDEBAR_BAR_H);
    it.box->addView(it.bar);

    it.badge_count = ws_make_label("", WS_BADGE_TEXT_SIZE, WS_C_TEXT);
    it.badge_count->setWidth(WS_BADGE_SIZE);
    it.badge_count->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    it.badge = ws_make_pill(it.badge_count, WS_C_ACCENT, WS_BADGE_SIZE, WS_BADGE_SIZE * 0.5f);
    it.badge->setWidth(WS_BADGE_SIZE);
    ws_place_absolute(it.badge, (WS_SIDEBAR_ITEM_W + WS_SIDEBAR_GLYPH_SIZE - WS_BADGE_SIZE) * 0.5f,
                      GLYPH_TOP - WS_BADGE_SIZE * 0.25f);
    it.badge->setVisibility(brls::Visibility::INVISIBLE);
    it.box->addView(it.badge);

    addView(it.box);
    items_.push_back(it);
    const size_t idx = items_.size() - 1;
    applyActive(idx, idx == active_);
    printf("[ui] sidebar: item %zu '%s' added\n", idx, label.c_str());
    return idx;
}

void IconSidebar::applyActive(size_t item, bool on) {
    if (item >= items_.size()) return;
    ws_show(items_[item].bar, on);
    items_[item].label->setTextColor(ws_nvg(on ? WS_C_ACCENT_TEXT : WS_C_TEXT));
}

void IconSidebar::setActive(size_t item) {
    if (item >= items_.size()) {
        printf("[ui] sidebar: setActive(%zu) out of range (%zu items)\n", item, items_.size());
        return;
    }
    applyActive(active_, false);
    active_ = item;
    applyActive(active_, true);
    printf("[ui] sidebar: active -> %zu\n", item);
}

void IconSidebar::setBadge(size_t item, int count) {
    if (item >= items_.size()) return;
    Item& it = items_[item];
    if (count > 0) it.badge_count->setText(std::to_string(count));
    ws_show(it.badge, count > 0);
    printf("[ui] sidebar: badge[%zu] = %d\n", item, count);
}

brls::View* IconSidebar::getDefaultFocus() {
    if (active_ < items_.size()) return items_[active_].box;
    return brls::Box::getDefaultFocus();
}

void IconSidebar::onChildFocusGained(brls::View* directChild, brls::View* focusedView) {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].box != directChild) continue;
        printf("[ui] sidebar: focus item %zu\n", i);
        if (on_focus_item_) on_focus_item_(i);
        break;
    }
    brls::Box::onChildFocusGained(directChild, focusedView);
}
