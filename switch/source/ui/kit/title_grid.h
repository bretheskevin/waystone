#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>
#include "ws_style.h"

// 128x128 focusable game tile; grows to WS_TILE_FOCUS around its center while focused.
class TitleTile : public brls::Box {
  public:
    TitleTile();

    // Icon at `path` (may be empty), or a `placeholder` rect when it cannot be loaded.
    void setArt(const std::string& path, NVGcolor placeholder);
    void setCornerBadge(const std::string& text);
    void setMarker(TileMarker marker);

    void onFocusGained() override;
    void onFocusLost() override;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

    bool isScaled() const { return scale_ > 1.0f; }

  private:
    void animateScale(float target);

    brls::Animatable scale_ = 1.0f;
    brls::View* art_        = nullptr;
    brls::Box* badge_       = nullptr;
    brls::Label* marker_    = nullptr;
};

// Absolute-positioned tile grid with 2D navigation; the focused tile is drawn last.
// Meant as a ScrollingFrame content view: it sets its own explicit width/height.
class TitleGrid : public brls::Box {
  public:
    explicit TitleGrid(size_t cols = WS_GRID_COLS);

    void addTile(TitleTile* tile);
    size_t focusedIndex() const { return focused_; }
    void focusIndex(size_t idx);
    void setOnFocusChanged(std::function<void(size_t)> cb) { on_focus_changed_ = std::move(cb); }

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;
    brls::View* getDefaultFocus() override;
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;
    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    bool jump(bool down);

    size_t cols_;
    size_t focused_ = 0;
    std::vector<TitleTile*> tiles_;
    std::function<void(size_t)> on_focus_changed_;
};
