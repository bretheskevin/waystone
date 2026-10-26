#include "menu_list.h"
#include <cstdio>
#include <vector>
#include "ws_style.h"
#include "ws_views.h"
#include "../borealis_focus.h"
#include "../keymap_switch.h"

static constexpr float BAR_INSET_Y = 6.0f;

MenuRow::MenuRow() : brls::Box(brls::Axis::ROW) {
    setFocusable(true);
    setHeight(WS_ROW_H_SINGLE);
    setPaddingLeft(WS_ROW_INSET);
    setPaddingRight(WS_ROW_INSET);
    setAlignItems(brls::AlignItems::CENTER);

    auto* text = new brls::Box(brls::Axis::COLUMN);
    text->setGrow(1.0f);
    text->setJustifyContent(brls::JustifyContent::CENTER);
    title_ = ws_make_label("", WS_ROW_TITLE_SIZE, WS_C_TEXT);
    text->addView(title_);
    subtitle_ = ws_make_label("", WS_ROW_DETAIL_SIZE, WS_C_MUTED);
    subtitle_->setVisibility(brls::Visibility::GONE);
    text->addView(subtitle_);
    addView(text);

    detail_ = ws_make_label("", WS_ROW_DETAIL_SIZE, WS_C_MUTED);
    detail_->setVisibility(brls::Visibility::GONE);
    detail_->setMarginLeft(WS_ROW_INSET);
    addView(detail_);

    bar_ = ws_make_accent_bar(WS_ROW_BAR_X, BAR_INSET_Y, WS_ROW_BAR_W,
                              WS_ROW_H_SINGLE - 2.0f * BAR_INSET_Y);
    addView(bar_);
}

void MenuRow::setTitle(const std::string& title) { ws_set_label_text(title_, title); }

void MenuRow::setSubtitle(const std::string& subtitle) {
    const bool two_line = !subtitle.empty();
    ws_set_label_text(subtitle_, subtitle);
    subtitle_->setVisibility(two_line ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    const float h = two_line ? WS_ROW_H_DOUBLE : WS_ROW_H_SINGLE;
    setHeight(h);
    bar_->setHeight(h - 2.0f * BAR_INSET_Y);
}

void MenuRow::setDetail(const std::string& detail) {
    ws_set_label_text(detail_, detail);
    detail_->setVisibility(detail.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
}

void MenuRow::setActive(bool active) {
    active_ = active;
    refreshAccent();
}

void MenuRow::refreshAccent() {
    const bool on = active_ || isFocused();
    ws_show(bar_, on);
    title_->setTextColor(ws_nvg(on ? WS_C_ACCENT_TEXT : WS_C_TEXT));
}

void MenuRow::onFocusGained() {
    brls::Box::onFocusGained();
    refreshAccent();
}

void MenuRow::onFocusLost() {
    brls::Box::onFocusLost();
    refreshAccent();
}

MenuList::MenuList() : brls::Box(brls::Axis::COLUMN) {}

MenuRow* MenuList::addRow(const std::string& title, const std::string& detail,
                          std::function<void()> on_click) {
    auto* row = new MenuRow();
    row->setTitle(title);
    row->setDetail(detail);
    if (on_click) {
        row->registerAction(ws_label(WsAction::Select), ws_brls(WsAction::Select),
                            [cb = std::move(on_click)](brls::View*) {
                                cb();
                                return true;
                            },
                            false, brls::SOUND_CLICK);
    }
    addView(row);
    printf("[ui] menu: row %zu '%s' added\n", getChildren().size() - 1, title.c_str());
    return row;
}

MenuRow* MenuList::addTwoLineRow(const std::string& title, const std::string& subtitle,
                                 const std::string& detail, std::function<void()> on_click) {
    MenuRow* row = addRow(title, detail, std::move(on_click));
    row->setSubtitle(subtitle);
    return row;
}

void MenuList::clear() {
    std::vector<brls::View*> rows = getChildren();
    for (brls::View* v : rows) removeView(v);
    printf("[ui] menu: cleared %zu rows\n", rows.size());
}

size_t MenuList::focusedIndex() const {
    return borealis_focused_child_index(const_cast<MenuList*>(this));
}

void MenuList::focusIndex(size_t idx) {
    printf("[ui] menu: focusIndex -> %zu\n", idx);
    borealis_focus_child(this, idx, nullptr);
}
