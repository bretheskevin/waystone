#pragma once
#include <borealis.hpp>
#include "kit/menu_list.h"
#include "kit/ws_scroll_frame.h"
#include "kit/ws_style.h"

// Sizes a dashboard pane (right of the sidebar, content band) as a column.
inline void ws_setup_pane(brls::Box* pane) {
    pane->setWidth(WS_PANE_W);
    pane->setHeight(WS_CONTENT_H);
}

// Appends a MenuList of `width` inside a growing scroll frame to `parent`.
inline MenuList* ws_add_scroll_menu(brls::Box* parent, float width, brls::View** scroll_out = nullptr) {
    WsScrollFrame* scroll = ws_make_scroll(width);
    scroll->setGrow(1.0f);
    auto* list = new MenuList();
    list->setWidth(width);
    scroll->setContentView(list);
    parent->addView(scroll);
    if (scroll_out) *scroll_out = scroll;
    return list;
}

// Appends a scrolling MenuList whose row text starts WS_PANE_NAME_INSET from the pane's left.
inline MenuList* ws_add_pane_menu(brls::Box* pane, brls::View** scroll_out = nullptr) {
    MenuList* list = ws_add_scroll_menu(pane, WS_PANE_W, scroll_out);
    list->setPaddingLeft(WS_PANE_NAME_INSET - WS_ROW_INSET);
    list->setPaddingRight(WS_PANE_NAME_INSET - WS_ROW_INSET);
    return list;
}
