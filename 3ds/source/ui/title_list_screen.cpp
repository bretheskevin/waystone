#include "title_list_screen.h"
#include "settings_screen.h"
#include "conflict_screen.h"
#include "snapshot_screen.h"
#include "history_screen.h"
#include "sync_screen.h"
#include "no_internet_screen.h"
#include "net_status.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include "rom_parse.h"
#include "version.h"
#include <cstdio>

static const float ICON_SZ = 40.0f;

void TitleListScreen::init_icon_cache() {
    icon_cache_.assign(titles_.size(), 0);
}

TitleListScreen::TitleListScreen(Session* session)
    : session_(session), sync_all_btn_rect_()
{
    printf("[title_list] enumerating titles...\n");
    {
        WebDavCfg dav = session_->dav.as_cfg();
        titles_ = list_titles(session_->vault, dav);
    }
    printf("[title_list] found %zu titles\n", titles_.size());
    init_icon_cache();
}

TitleListScreen::TitleListScreen(Session* session,
                                 const std::vector<TitleInfo>& preloaded_titles)
    : session_(session), sync_all_btn_rect_()
{
    titles_ = preloaded_titles;
    init_icon_cache();
}

TitleListScreen::~TitleListScreen() {
    for (size_t i = 0; i < icon_cache_.size(); i++)
        free_icon_image(icon_cache_[i]);
    icon_cache_.clear();
}

static u32 system_tile_color(const std::string& system) {
    if (system == "nds")  return CLR_NEUTRAL_500;
    if (system == "gba")  return CLR_PRIMARY_700;
    if (system == "gb")   return CLR_SUCCESS;
    if (system == "nes")  return CLR_ERROR;
    if (system == "snes") return CLR_PRIMARY_500;
    if (system == "md")   return CLR_NEUTRAL_900;
    if (system == "sms")  return CLR_SYNC;
    if (system == "gg")   return CLR_WARNING;
    if (system == "ngp")  return CLR_NEUTRAL_800;
    return CLR_NEUTRAL_400;
}

static void draw_system_tile(C2D_TextBuf buf, const std::string& system, float x, float y) {
    draw_rounded_rect(x, y, 0.51f, ICON_SZ, ICON_SZ, RAD_MD, system_tile_color(system));
    const char* label = rom_system_badge(system);
    float th = text_height(buf, TEXT_BASE, label);
    draw_text_centered_fit(buf, x + (float)SP_XS, y + (ICON_SZ - th) / 2.0f, 0.52f, TEXT_BASE,
                           CLR_WHITE, label, ICON_SZ - 2.0f * (float)SP_XS, TEXT_SM);
}

// ---- ListScreen hooks ----

const char* TitleListScreen::corner_label() {
    return "v" WS_APP_VERSION;
}

void TitleListScreen::draw_row(C2D_TextBuf buf, size_t i,
                                float x, float y, float w, float h, bool focused) {
    (void)focused;
    float icon_x = x + (float)SP_SM;
    float icon_y = y + (h - ICON_SZ) / 2.0f;

    // Icon
    const TitleInfo& t = titles_[i];
    const bool is_rom = (t.source == SourceRom);
    const unsigned rom_side = is_rom ? rom_icon_side(t.icon.size()) : 0;
    const bool icon_ok = is_rom ? rom_side != 0 : !t.icon.empty();
    IconImage* icon = nullptr;
    if (i < icon_cache_.size() && icon_ok) {
        if (!icon_cache_[i]) {
            icon_cache_[i] = is_rom
                ? rgba5551_icon_to_image(t.icon.data(), rom_side, rom_side == BOXART_ICON_SIDE)
                : smdh_icon_to_image(t.icon.data());
            printf(icon_cache_[i]
                   ? "[ui] built icon tex row=%zu\n"
                   : "[ui] icon tex build failed row=%zu\n", i);
        }
        icon = icon_cache_[i];
    }
    if (icon)
        draw_image(icon->img, icon_x, icon_y, ICON_SZ, ICON_SZ);
    else if (is_rom)
        draw_system_tile(buf, t.system, icon_x, icon_y);
    else
        draw_rounded_rect(icon_x, icon_y, 0.51f, ICON_SZ, ICON_SZ, RAD_SM, CLR_NEUTRAL_200);
    if (is_rom && icon) {
        const char* badge = rom_system_badge(t.system);
        float bw = text_width(buf, TEXT_SM, badge) + 2.0f * (float)SP_XS;
        float bh = text_height(buf, TEXT_SM, badge) + 2.0f;
        float bx = icon_x + ICON_SZ - bw;
        float by = icon_y + ICON_SZ - bh;
        draw_rounded_rect(bx, by, 0.52f, bw, bh, RAD_SM, CLR_PRIMARY_600);
        draw_text(buf, bx + (float)SP_XS, by + 1.0f, 0.53f, TEXT_SM, CLR_WHITE, badge);
    }

    // Name with UTF-8-safe truncation
    float text_x     = icon_x + ICON_SZ + (float)SP_MD;
    float max_text_w = x + w - text_x - (float)SP_MD;
    float text_y     = y + (h - text_height(buf, TEXT_BASE, "A")) / 2.0f;

    std::string display = truncate_text_fit(buf, TEXT_BASE, t.name.c_str(), max_text_w);
    draw_text(buf, text_x, text_y, 0.51f, TEXT_BASE, CLR_TEXT, display.c_str());
}

void TitleListScreen::draw_detail(C2D_TextBuf buf, float area_y, float area_h) {
    (void)area_h;
    static const float BTN_W = 140.0f;
    static const float BTN_H = 28.0f;
    float btn_x = ((float)SCREEN_BOT_W - BTN_W) / 2.0f;
    float btn_y = area_y + (float)SP_SM;
    bool enabled = !titles_.empty();
    if (enabled) {
        sync_all_btn_rect_ = draw_button(buf, btn_x, btn_y, BTN_W, BTN_H,
                                         ws_label(WsAction::SyncAll), ButtonStyle::PRIMARY, /*focused=*/false);
    } else {
        draw_rounded_rect(btn_x, btn_y, 0.5f, BTN_W, BTN_H, RAD_MD, CLR_NEUTRAL_200);
        float th = text_height(buf, TEXT_BASE, ws_label(WsAction::SyncAll));
        draw_text_centered(buf, btn_x, btn_y + (BTN_H - th) / 2.0f,
                           0.51f, TEXT_BASE, CLR_NEUTRAL_400, ws_label(WsAction::SyncAll), BTN_W);
        sync_all_btn_rect_ = {btn_x, btn_y, BTN_W, BTN_H};
    }
}

std::vector<Action> TitleListScreen::actions() {
    bool has_sel = !titles_.empty() && cursor_ < titles_.size();
    bool has_any = !titles_.empty();
    std::vector<Action> a;
    a.push_back(make_action(WsAction::SyncGame,      ACT_SYNC,      has_sel, ButtonStyle::PRIMARY));
    a.push_back(make_action(WsAction::SyncAll,       ACT_SYNC_ALL,  has_any, ButtonStyle::SECONDARY));
    a.push_back(make_action(WsAction::OpenConflicts, ACT_CONFLICTS, true,    ButtonStyle::SECONDARY));
    a.push_back(make_action(WsAction::OpenSettings,  ACT_SETTINGS,  true,    ButtonStyle::SECONDARY));
    a.push_back(make_action(WsAction::OpenSnapshots, ACT_SNAPSHOTS, has_sel, ButtonStyle::SECONDARY));
    a.push_back(make_action(WsAction::OpenHistory,   ACT_HISTORY,   has_sel, ButtonStyle::SECONDARY));
    return a;
}

void TitleListScreen::on_action(int id) {
    switch (id) {
    case ACT_SYNC:
        if (cursor_ < titles_.size()) {
            printf("[sync] sync single: %s (id=0x%05x)\n",
                   titles_[cursor_].name.c_str(), (unsigned int)titles_[cursor_].unique_id);
            start_sync_or_gate({titles_[cursor_]});
        }
        break;
    case ACT_SYNC_ALL:
        printf("[ui] sync all triggered (%zu titles)\n", titles_.size());
        printf("[sync] sync all: %zu titles\n", titles_.size());
        start_sync_or_gate(titles_);
        break;
    case ACT_CONFLICTS:
        App::instance().push_screen(new ConflictScreen(session_, titles_));
        break;
    case ACT_SETTINGS:
        App::instance().push_screen(new SettingsScreen(session_));
        break;
    case ACT_SNAPSHOTS:
        if (cursor_ < titles_.size())
            App::instance().push_screen(
                new SnapshotScreen(session_, titles_[cursor_]));
        break;
    case ACT_HISTORY:
        if (cursor_ < titles_.size())
            App::instance().push_screen(
                new HistoryScreen(session_, titles_[cursor_]));
        break;
    }
}

// ---- handle_input ----

void TitleListScreen::handle_input(u32 kDown, touchPosition touch) {
    // Check Sync All touch first; zero the touch so no action-bar button
    // fires on the same tap, but let kDown through to the base class.
    if (touch.px != 0 || touch.py != 0) {
        if (sync_all_btn_rect_.contains((float)touch.px, (float)touch.py)) {
            if (!titles_.empty()) {
                printf("[ui] sync all triggered (tap, %zu titles)\n", titles_.size());
                start_sync_or_gate(titles_);
            } else {
                printf("[ui] Sync All button tapped (no-op: no titles)\n");
            }
            touch.px = 0;
            touch.py = 0;
        }
    }
    ListScreen::handle_input(kDown, touch);
}

// ---- Sync helpers ----

void TitleListScreen::start_sync_or_gate(std::vector<TitleInfo> titles) {
    if (!network_available()) {
        App::instance().push_screen(
            new NoInternetScreen(NoInternetReason::NoNetwork, [this, titles]() {
                start_sync(titles);
            }));
        return;
    }
    start_sync(titles);
}

void TitleListScreen::start_sync(std::vector<TitleInfo> titles) {
    if (titles.empty()) { printf("[sync] no titles to sync\n"); return; }
    printf("[sync] opening modal loader for %zu title(s)\n", titles.size());
    // Push a modal loader that owns the worker and blocks all input until the
    // sync finishes — the title list underneath is neither polled nor drawn.
    App::instance().push_screen(new SyncScreen(session_, titles));
}
