#include "title_grid.h"
#include <cstdio>
#include "grid_nav.h"
#include "ws_views.h"
#include "../keymap_switch.h"

static constexpr float BADGE_H      = 20.0f;
static constexpr float BADGE_PAD    = 6.0f;
static constexpr float BADGE_RADIUS = 4.0f;
static constexpr float EXTRA_INSET  = 4.0f;

TitleTile::TitleTile() : brls::Box(brls::Axis::COLUMN) {
    setWidth(WS_TILE);
    setHeight(WS_TILE);
    setFocusable(true);
}

void TitleTile::setArt(const std::string& path, NVGcolor placeholder) {
    if (art_) removeView(art_);
    art_ = ws_add_icon_art(this, path, placeholder, WS_TILE, 0);
}

void TitleTile::setCornerBadge(const std::string& text) {
    if (badge_) {
        removeView(badge_);
        badge_ = nullptr;
    }
    if (text.empty()) return;
    badge_ = ws_make_pill(ws_make_label(text, WS_TILE_BADGE_TEXT_SIZE, WS_C_TEXT), WS_C_BADGE_BG,
                          BADGE_H, BADGE_RADIUS);
    badge_->setPaddingLeft(BADGE_PAD);
    badge_->setPaddingRight(BADGE_PAD);
    badge_->setPositionType(brls::PositionType::ABSOLUTE);
    badge_->setPositionRight(EXTRA_INSET);
    badge_->setPositionBottom(EXTRA_INSET);
    addView(badge_);
}

void TitleTile::setMarker(TileMarker marker) {
    if (marker_) {
        removeView(marker_);
        marker_ = nullptr;
    }
    if (marker == TileMarker::None) return;
    const bool conflict = marker == TileMarker::Conflict;
    marker_ = ws_make_label(conflict ? WS_ICON_WARNING : WS_ICON_ERROR, WS_TILE_MARKER_SIZE,
                            conflict ? WS_C_WARNING : WS_C_ERROR);
    marker_->setPositionType(brls::PositionType::ABSOLUTE);
    marker_->setPositionRight(EXTRA_INSET);
    marker_->setPositionTop(EXTRA_INSET);
    addView(marker_);
}

void TitleTile::animateScale(float target) {
    scale_.reset();
    scale_.addStep(target, WS_ANIM_TILE_MS, brls::EasingFunction::quadraticOut);
    scale_.start();
}

void TitleTile::onFocusGained() {
    brls::Box::onFocusGained();
    animateScale(WS_TILE_FOCUS_SCALE);
}

void TitleTile::onFocusLost() {
    brls::Box::onFocusLost();
    animateScale(1.0f);
}

void TitleTile::draw(NVGcontext* vg, float x, float y, float width, float height,
                     brls::Style style, brls::FrameContext* ctx) {
    const float s = scale_;
    setHighlightPadding(tile_highlight_padding(s));
    if (s <= 1.0f) {
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        return;
    }
    const float cx = x + width * 0.5f;
    const float cy = y + height * 0.5f;
    nvgSave(vg);
    nvgTranslate(vg, cx, cy);
    nvgScale(vg, s, s);
    nvgTranslate(vg, -cx, -cy);
    ws_nvg_fill_rect(vg, x, y, width, height, 0.0f, WS_C_BG);
    brls::Box::draw(vg, x, y, width, height, style, ctx);
    nvgRestore(vg);
}

TitleGrid::TitleGrid(size_t cols) : brls::Box(brls::Axis::COLUMN), cols_(cols ? cols : 1) {
    setWidth(grid_content_width(cols_));
    setHeight(grid_content_height(0, cols_));
    registerAction(ws_label(WsAction::JumpUp), ws_brls(WsAction::JumpUp),
                   [this](brls::View*) { return jump(false); });
    registerAction(ws_label(WsAction::JumpDown), ws_brls(WsAction::JumpDown),
                   [this](brls::View*) { return jump(true); });
}

void TitleGrid::addTile(TitleTile* tile) {
    float x = 0.0f, y = 0.0f;
    grid_cell_origin(tiles_.size(), cols_, x, y);
    ws_place_absolute(tile, x, y);
    addView(tile);
    tiles_.push_back(tile);
    setHeight(grid_content_height(tiles_.size(), cols_));
    printf("[ui] grid: tile %zu added at (%.0f,%.0f), rows=%zu\n", tiles_.size() - 1, x, y,
           grid_rows(tiles_.size(), cols_));
}

void TitleGrid::focusIndex(size_t idx) {
    if (tiles_.empty()) {
        printf("[ui] grid: focusIndex(%zu) ignored, grid empty\n", idx);
        return;
    }
    if (idx >= tiles_.size()) idx = tiles_.size() - 1;
    focused_ = idx;
    printf("[ui] grid: focusIndex -> %zu\n", idx);
    brls::Application::giveFocus(tiles_[idx]);
}

static GridDir to_grid_dir(brls::FocusDirection d) {
    switch (d) {
    case brls::FocusDirection::UP:    return GridDir::Up;
    case brls::FocusDirection::DOWN:  return GridDir::Down;
    case brls::FocusDirection::LEFT:  return GridDir::Left;
    case brls::FocusDirection::RIGHT: return GridDir::Right;
    }
    return GridDir::Up;
}

brls::View* TitleGrid::getNextFocus(brls::FocusDirection direction, brls::View* currentView) {
    const size_t idx = *static_cast<size_t*>(currentView->getParentUserData());
    const size_t next = grid_move(idx, to_grid_dir(direction), tiles_.size(), cols_);
    if (next == GRID_NO_MOVE) return nullptr;
    return tiles_[next];
}

brls::View* TitleGrid::getDefaultFocus() {
    if (tiles_.empty()) return nullptr;
    return tiles_[focused_ < tiles_.size() ? focused_ : tiles_.size() - 1];
}

void TitleGrid::onChildFocusGained(brls::View* directChild, brls::View* focusedView) {
    const size_t idx = *static_cast<size_t*>(directChild->getParentUserData());
    if (idx < tiles_.size() && idx != focused_) {
        focused_ = idx;
        if (on_focus_changed_) on_focus_changed_(idx);
    }
    brls::Box::onChildFocusGained(directChild, focusedView);
}

bool TitleGrid::jump(bool down) {
    const size_t next = grid_move(focused_, down ? GridDir::PageDown : GridDir::PageUp,
                                  tiles_.size(), cols_);
    if (next == GRID_NO_MOVE) {
        printf("[ui] grid: jump %s from %zu: edge\n", down ? "down" : "up", focused_);
        return true;
    }
    printf("[ui] grid: jump %s %zu -> %zu\n", down ? "down" : "up", focused_, next);
    brls::Application::giveFocus(tiles_[next]);
    return true;
}

// Mirrors Box::draw but paints growing/shrinking tiles after the rest, focused one last.
void TitleGrid::draw(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext* ctx) {
    TitleTile* top = tiles_.empty() ? nullptr : tiles_[focused_ < tiles_.size() ? focused_ : 0];
    for (TitleTile* t : tiles_)
        if (t != top && !t->isScaled()) t->frame(ctx);
    for (TitleTile* t : tiles_)
        if (t != top && t->isScaled()) t->frame(ctx);
    if (top) top->frame(ctx);
}
