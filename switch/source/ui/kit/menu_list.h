#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>

// JKSV-style text row: single-line (title) or two-line (title + muted subtitle), optional
// right-aligned detail. Accent bar + accent title while focused or active.
class MenuRow : public brls::Box {
  public:
    MenuRow();

    void setTitle(const std::string& title);
    void setSubtitle(const std::string& subtitle);
    void setDetail(const std::string& detail);
    void setActive(bool active);

    void onFocusGained() override;
    void onFocusLost() override;

  private:
    void refreshAccent();

    brls::Label* title_     = nullptr;
    brls::Label* subtitle_  = nullptr;
    brls::Label* detail_    = nullptr;
    brls::Rectangle* bar_   = nullptr;
    bool active_            = false;
};

class MenuList : public brls::Box {
  public:
    MenuList();

    MenuRow* addRow(const std::string& title, const std::string& detail = "",
                    std::function<void()> on_click = {});
    MenuRow* addTwoLineRow(const std::string& title, const std::string& subtitle,
                           const std::string& detail, std::function<void()> on_click);

    // Deletes every row. If a row is focused (e.g. called from a row's own action), callers
    // MUST defer this through DeferredRefreshPump — deleting the focused view is a UAF.
    void clear();

    size_t focusedIndex() const;
    void focusIndex(size_t idx);
};
