#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>
#include "dashboard_model.h"

class TitleGrid;
class TitleTile;

// Placeholder art color when a title has no loadable icon (per-system tint for ROMs).
NVGcolor title_placeholder_color(const TitleInfo& t);

// Dashboard Games / Emulator pane: name strip (focused game + detail) over a JKSV icon grid.
// A on a tile calls the open callback. Back/Quit belong to the dashboard.
class GamesPane : public brls::Box {
  public:
    GamesPane(const std::vector<TitleInfo>& all, Section s);

    // Indexed like the `all` titles; updates markers + strip in place.
    void setResults(const std::vector<TitleResult>& results_by_title_index);
    const TitleInfo* focusedTitle() const;
    std::string focusedDetail() const;
    void setOnOpen(std::function<void(const TitleInfo&)> cb) { on_open_ = std::move(cb); }

    brls::View* getDefaultFocus() override;

  private:
    const TitleResult* resultFor(size_t title_index) const;
    void refreshStrip();

    std::vector<TitleInfo>   all_;
    std::vector<size_t>      indices_;
    std::vector<TitleResult> results_;
    std::vector<TitleTile*>  tiles_;
    const char*              tag_;
    TitleGrid*   grid_   = nullptr;
    brls::Label* name_   = nullptr;
    brls::Label* detail_ = nullptr;
    std::function<void(const TitleInfo&)> on_open_;
};
