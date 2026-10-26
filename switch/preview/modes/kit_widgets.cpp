// Kit widget preview modes: kit-widgets, kit-widgets-scrolled, kit-menu.
#include "registry.h"
#include "kit_preview.h"
#include <borealis.hpp>
#include <cstdio>
#include "kit/icon_sidebar.h"
#include "kit/menu_list.h"
#include "kit/title_grid.h"
#include "kit/ws_nvg.h"
#include "kit/ws_scroll_frame.h"
#include "kit/ws_style.h"

static const size_t FIXTURE_TILES = 24;

// Root that leaves the header band (y 0-88) empty and gives the content area its 558 px.
static brls::Box* make_screen(brls::Box*& content) {
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setWidth(WS_SCREEN_W);
    root->setHeight(WS_SCREEN_H);
    root->setBackgroundColor(ws_nvg(WS_C_BG));
    root->setPaddingTop(WS_CONTENT_Y);
    content = new brls::Box(brls::Axis::ROW);
    content->setHeight(WS_CONTENT_H);
    root->addView(content);
    return root;
}

// Focus after the first layout so the ScrollingFrame computes the scroll from real positions.
class KitPreviewActivity : public brls::Activity {
  public:
    explicit KitPreviewActivity(std::function<void()> focus) : focus_(std::move(focus)) {}
    void onContentAvailable() override {
        timer_.setEndCallback([this](bool) { focus_(); });
        timer_.start(300);
    }

  protected:
    std::function<void()> focus_;
    brls::Timer timer_;
};

class KitWidgetsActivity : public KitPreviewActivity {
  public:
    explicit KitWidgetsActivity(size_t focus_tile)
        : KitPreviewActivity([this, focus_tile] { grid_->focusIndex(focus_tile); }) {}

    brls::View* createContentView() override {
        brls::Box* content = nullptr;
        auto* root         = make_screen(content);

        auto* sidebar = new IconSidebar();
        sidebar->addItem(WS_ICON_GAMES, "Games");
        sidebar->addItem(WS_ICON_EMULATOR, "Emulator");
        const size_t conflicts = sidebar->addItem(WS_ICON_CONFLICTS, "Conflicts");
        sidebar->addItem(WS_ICON_SETTINGS, "Settings");
        sidebar->setActive(0);
        sidebar->setBadge(conflicts, 3);
        content->addView(sidebar);

        WsScrollFrame* scroll = ws_make_scroll(WS_PANE_W);
        scroll->setMarginLeft(WS_PANE_X - WS_SIDEBAR_W);
        scroll->setHeight(WS_CONTENT_H);

        grid_ = new TitleGrid();
        for (size_t i = 0; i < FIXTURE_TILES; i++) {
            auto* tile = new TitleTile();
            if (i == 3 || i == 10) {
                tile->setArt("", nvgHSL(i == 3 ? 0.62f : 0.08f, 0.45f, 0.42f));
                tile->setCornerBadge(i == 3 ? "GBA" : "DS");
            } else {
                tile->setArt(FIXTURE_ICON, ws_nvg(WS_C_PLACEHOLDER));
            }
            if (i == 5) tile->setMarker(TileMarker::Conflict);
            if (i == 12) tile->setMarker(TileMarker::Failed);
            grid_->addTile(tile);
        }
        scroll->setContentView(grid_);
        content->addView(scroll);
        return root;
    }

  private:
    TitleGrid* grid_ = nullptr;
};

class KitMenuActivity : public KitPreviewActivity {
  public:
    KitMenuActivity() : KitPreviewActivity([this] { menu_->focusIndex(1); }) {}

    brls::View* createContentView() override {
        brls::Box* content = nullptr;
        auto* root         = make_screen(content);

        menu_ = new MenuList();
        menu_->setMarginLeft(WS_PANE_X);
        menu_->setWidth(WS_PANE_W);
        auto noop = [] { printf("[ui] kit-menu: row clicked\n"); };
        menu_->addRow("Sync now", "", noop);
        menu_->addRow("Auto sync", "On", noop);
        menu_->addRow("Snapshots", "", noop);
        menu_->addTwoLineRow("Pokemon Emerald", "Last synced 2 h ago", "GBA", noop);
        MenuRow* active = menu_->addTwoLineRow("Mario Kart DS", "Conflict pending", "", noop);
        active->setActive(true);
        content->addView(menu_);
        return root;
    }

  private:
    MenuList* menu_ = nullptr;
};

static void run_kit_widgets(Session&) { brls::Application::pushActivity(new KitWidgetsActivity(8)); }
static void run_kit_widgets_scrolled(Session&) {
    brls::Application::pushActivity(new KitWidgetsActivity(22));
}
static void run_kit_menu(Session&) { brls::Application::pushActivity(new KitMenuActivity()); }

static PreviewModeReg reg_kit_widgets("kit-widgets", &run_kit_widgets);
static PreviewModeReg reg_kit_widgets_scrolled("kit-widgets-scrolled", &run_kit_widgets_scrolled);
static PreviewModeReg reg_kit_menu("kit-menu", &run_kit_menu);
