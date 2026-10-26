#include "game_panel.h"
#include <cstdio>
#include "games_pane.h"
#include "history_controller.h"
#include "history_panel.h"
#include "keymap_switch.h"
#include "snapshots_controller.h"
#include "snapshots_panel.h"
#include "kit/menu_list.h"
#include "kit/ws_views.h"

static constexpr float ICON_SIZE      = 96.0f;
static constexpr float NAME_SIZE      = 24.0f;
static constexpr float NAME_LINES     = 2.0f;
static constexpr float HEADER_GAP     = 12.0f;
static const char* const GLYPH_SEP    = "  ";

static std::string glyph_row(const char* glyph, const char* text) {
    return std::string(glyph) + GLYPH_SEP + text;
}

GamePanel::GamePanel(TitleInfo title, Session* session, std::string detail,
                     std::function<void(TitleInfo)> on_sync_now)
    : SlidePanel(WIDTH, true), title_(std::move(title)), session_(session),
      detail_(std::move(detail)), on_sync_now_(std::move(on_sync_now)) {
    setGuide(ws_hint_bar({WsAction::Select, WsAction::Back}));
}

void GamePanel::buildBody(brls::Box* body) {
    printf("[ui] game panel: build for '%s'\n", title_.name.c_str());
    padBody(body);

    ws_add_icon_art(body, title_.icon_path, title_placeholder_color(title_), ICON_SIZE)
        ->setMarginLeft(WS_ROW_INSET);

    brls::Label* name =
        addInsetText(body, ws_make_text(title_.name.empty() ? " " : title_.name, NAME_SIZE, WS_C_TEXT), HEADER_GAP);
    name->setMaxHeight(NAME_SIZE * 1.4f * NAME_LINES);

    addInsetText(body, ws_make_label(detail_, WS_ROW_DETAIL_SIZE, WS_C_MUTED), 4.0f)
        ->setMarginBottom(WS_DIALOG_PAD / 2.0f);

    auto* menu = new MenuList();
    menu->setWidth(WIDTH);
    menu->addRow(glyph_row(WS_ICON_SYNC, "Sync now"), "", [this] {
        printf("[ui] game panel: sync now '%s'\n", title_.name.c_str());
        close([cb = on_sync_now_, t = title_] { if (cb) cb(t); });
    });
    menu->addRow(glyph_row(WS_ICON_HISTORY, "History"), "", [this] { openHistory(); });
    menu->addRow(glyph_row(WS_ICON_SNAPSHOTS, "Snapshots"), "", [this] { openSnapshots(); });
    body->addView(menu);
}

void GamePanel::openHistory() {
    printf("[ui] game panel: open history '%s'\n", title_.name.c_str());
    auto* hc = new HistoryController(title_, session_);
    hc->start_scan();
    OverlayActivity::push(new HistoryPanel(hc));
}

void GamePanel::openSnapshots() {
    printf("[ui] game panel: open snapshots '%s'\n", title_.name.c_str());
    auto* sc = new SnapshotsController(title_, session_->uid);
    sc->start_scan();
    OverlayActivity::push(new SnapshotsPanel(sc));
}
