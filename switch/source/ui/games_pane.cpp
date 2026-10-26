#include "games_pane.h"
#include <cstdio>
#include "keymap_switch.h"
#include "pane_menu.h"
#include "kit/title_grid.h"
#include "kit/ws_scroll_frame.h"
#include "kit/ws_views.h"

static constexpr float EMPTY_TEXT_SIZE = 20.0f;
static constexpr float STRIP_GAP       = 16.0f;

NVGcolor title_placeholder_color(const TitleInfo& t) {
    if (t.source == SourceRom) return nvgHSL((float)(rom_fnv1a(t.system) % 360) / 360.0f, 0.45f, 0.42f);
    return ws_nvg(WS_C_PLACEHOLDER);
}

static TitleTile* make_tile(const TitleInfo& t) {
    auto* tile = new TitleTile();
    tile->setArt(t.icon_path, title_placeholder_color(t));
    if (t.source == SourceRom) tile->setCornerBadge(rom_system_badge(t.system));
    return tile;
}

GamesPane::GamesPane(const std::vector<TitleInfo>& all, Section s)
    : brls::Box(brls::Axis::COLUMN), all_(all), indices_(section_title_indices(all, s)),
      tag_(s == Section::Emulator ? "emulator" : "games") {
    ws_setup_pane(this);

    if (indices_.empty()) {
        setJustifyContent(brls::JustifyContent::CENTER);
        setAlignItems(brls::AlignItems::CENTER);
        addView(ws_make_label(s == Section::Emulator ? "No emulator saves found"
                                                     : "No Switch saves found for this account",
                              EMPTY_TEXT_SIZE, WS_C_MUTED));
        printf("[ui] %s pane: empty\n", tag_);
        return;
    }

    auto* strip = new brls::Box(brls::Axis::ROW);
    strip->setWidth(WS_PANE_W);
    strip->setHeight(WS_PANE_STRIP_H);
    strip->setAlignItems(brls::AlignItems::CENTER);
    strip->setPaddingLeft(WS_PANE_NAME_INSET);
    strip->setPaddingRight(WS_PANE_NAME_INSET);
    name_ = ws_make_label("", WS_PANE_NAME_SIZE, WS_C_TEXT);
    name_->setGrow(1.0f);
    name_->setShrink(1.0f);
    strip->addView(name_);
    detail_ = ws_make_label("", WS_PANE_DETAIL_SIZE, WS_C_MUTED);
    detail_->setShrink(0.0f);
    detail_->setMarginLeft(STRIP_GAP);
    strip->addView(detail_);
    addView(strip);

    WsScrollFrame* scroll = ws_make_scroll(WS_PANE_W);
    scroll->setHeight(WS_CONTENT_H - WS_PANE_STRIP_H);
    grid_ = new TitleGrid();
    for (size_t ti : indices_) {
        TitleTile* tile = make_tile(all_[ti]);
        tiles_.push_back(tile);
        grid_->addTile(tile);
    }
    grid_->setOnFocusChanged([this](size_t) { refreshStrip(); });
    grid_->registerAction(ws_label(WsAction::OpenGame), ws_brls(WsAction::OpenGame), [this](brls::View*) {
        const TitleInfo* t = focusedTitle();
        if (!t) return true;
        printf("[ui] %s pane: open '%s'\n", tag_, t->name.c_str());
        if (on_open_) on_open_(*t);
        return true;
    });
    scroll->setContentView(grid_);
    addView(scroll);
    refreshStrip();
    printf("[ui] %s pane: %zu tiles\n", tag_, tiles_.size());
}

const TitleResult* GamesPane::resultFor(size_t title_index) const {
    return title_index < results_.size() ? &results_[title_index] : nullptr;
}

void GamesPane::setResults(const std::vector<TitleResult>& results_by_title_index) {
    results_ = results_by_title_index;
    size_t marked = 0;
    for (size_t i = 0; i < tiles_.size(); i++) {
        const TitleResult* r = resultFor(indices_[i]);
        const TileMarker m   = r ? tile_marker(r->state) : TileMarker::None;
        if (m != TileMarker::None) marked++;
        tiles_[i]->setMarker(m);
    }
    if (!tiles_.empty()) refreshStrip();
    printf("[ui] %s pane: results applied (%zu results, %zu tiles marked)\n", tag_, results_.size(), marked);
}

const TitleInfo* GamesPane::focusedTitle() const {
    if (!grid_ || indices_.empty()) return nullptr;
    const size_t i = grid_->focusedIndex();
    return i < indices_.size() ? &all_[indices_[i]] : nullptr;
}

std::string GamesPane::focusedDetail() const {
    if (!grid_ || indices_.empty()) return "";
    const size_t i = grid_->focusedIndex();
    if (i >= indices_.size()) return "";
    return tile_detail(all_[indices_[i]], resultFor(indices_[i]));
}

void GamesPane::refreshStrip() {
    const TitleInfo* t = focusedTitle();
    if (!t) return;
    ws_set_label_text(name_, t->name);
    ws_set_label_text(detail_, focusedDetail());
}

brls::View* GamesPane::getDefaultFocus() {
    return grid_ ? grid_->getDefaultFocus() : nullptr;
}
