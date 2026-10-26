#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

// JKSV-style left icon column: focusable glyph+label items, one active.
class IconSidebar : public brls::Box {
  public:
    IconSidebar();

    size_t addItem(const char* glyph_utf8, const std::string& label);
    void setActive(size_t item);
    void setBadge(size_t item, int count);
    void setOnFocusItem(std::function<void(size_t)> cb) { on_focus_item_ = std::move(cb); }

    brls::View* getDefaultFocus() override;
    void onChildFocusGained(brls::View* directChild, brls::View* focusedView) override;

  private:
    struct Item {
        brls::Box* box;
        brls::Label* label;
        brls::Rectangle* bar;
        brls::Box* badge;
        brls::Label* badge_count;
    };
    void applyActive(size_t item, bool on);

    std::vector<Item> items_;
    size_t active_ = 0;
    std::function<void(size_t)> on_focus_item_;
};
