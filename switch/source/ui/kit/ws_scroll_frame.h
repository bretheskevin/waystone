#pragma once
#include <borealis.hpp>

// brls::ScrollingFrame with two fixes for kit panes (grid, menus):
//  1. willAppear(true) arms a non-animated updateScrolling() that sets scrollY WITHOUT moving
//     the content (the translation is only applied from the animation tick) and also stops the
//     focus scroll animation started in the same frame -> the focused item stays off-screen and
//     every later scroll is offset. Forwarding resetState=false keeps only the animated path
//     (and keeps the scroll position when the activity reappears).
//  2. onLayout() sets the content's detached origin before Yoga has positioned the frame on its
//     parent's main axis (content lands at x=0 when the frame sits right of a sidebar), so the
//     origin is re-synced from the frame every draw. Vertical scrolling uses translationY.
class WsScrollFrame : public brls::ScrollingFrame {
  public:
    void willAppear(bool) override { brls::ScrollingFrame::willAppear(false); }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override {
        if (!getChildren().empty()) getChildren()[0]->setDetachedPosition(getX(), getY());
        brls::ScrollingFrame::draw(vg, x, y, width, height, style, ctx);
    }
};

inline WsScrollFrame* ws_make_scroll(float width) {
    auto* scroll = new WsScrollFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    scroll->setWidth(width);
    return scroll;
}
