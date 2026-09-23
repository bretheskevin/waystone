#include "title_list_screen.h"
#include "settings_screen.h"
#include "conflict_screen.h"
#include "snapshot_screen.h"
#include "history_screen.h"
#include "no_internet_screen.h"
#include "net_status.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

static const float ICON_SZ = 40.0f;

void TitleListScreen::init_icon_cache() {
    icon_cache_.assign(titles_.size(), 0);
}

TitleListScreen::TitleListScreen(Session* session)
    : session_(session), worker_(0), syncing_(false)
{
    printf("[title_list] enumerating titles...\n");
    titles_ = list_titles();
    printf("[title_list] found %zu titles\n", titles_.size());
    init_icon_cache();
    status_text_ = "";
}

TitleListScreen::TitleListScreen(Session* session,
                                 const std::vector<TitleInfo>& preloaded_titles)
    : session_(session), worker_(0), syncing_(false)
{
    titles_ = preloaded_titles;
    init_icon_cache();
    status_text_ = "";
}

TitleListScreen::~TitleListScreen() {
    for (size_t i = 0; i < icon_cache_.size(); i++)
        free_icon_image(icon_cache_[i]);
    icon_cache_.clear();
    if (worker_) { worker_->join(); delete worker_; }
}

// ---- poll (SyncWorker) ----

void TitleListScreen::poll() {
    if (!worker_ || !syncing_) return;
    SyncPhase ph = worker_->phase();
    status_text_ = worker_->status();
    if (ph == SyncPhase::Done || ph == SyncPhase::Error) {
        syncing_ = false;
        printf("[title_list] sync finished: %s\n",
               ph == SyncPhase::Done ? "done" : "error");
    }
}

// ---- ListScreen hooks ----

float TitleListScreen::status_area_height() const {
    if (worker_ && syncing_) return 50.0f;   // status + counters + progress bar
    if (!status_text_.empty()) return 20.0f;  // post-sync result / error text
    return 0.0f;
}

void TitleListScreen::draw_top_status(C2D_TextBuf buf, float sy) {
    u32 status_color = CLR_SYNC;
    if (worker_) {
        SyncPhase ph = worker_->phase();
        if (ph == SyncPhase::Error) status_color = CLR_ERROR;
        else if (ph == SyncPhase::Done) status_color = CLR_SUCCESS;
    }
    draw_text_centered(buf, 0, sy, 0.5f, TEXT_BASE, status_color,
                       status_text_.c_str(), (float)SCREEN_TOP_W);

    if (worker_ && worker_->phase() == SyncPhase::Running) {
        int total = worker_->total_count();
        int done  = worker_->pushed_count() + worker_->restored_count();
        float progress = (total > 0) ? (float)done / (float)(total * 2) : 0.0f;
        char prog_text[64];
        snprintf(prog_text, sizeof(prog_text), "Pushed: %d  Restored: %d",
                 worker_->pushed_count(), worker_->restored_count());
        draw_text_centered(buf, 0, sy + 18.0f, 0.5f, TEXT_SM,
                           CLR_NEUTRAL_400, prog_text, (float)SCREEN_TOP_W);
        draw_progress_bar(50.0f, sy + 34.0f,
                          (float)SCREEN_TOP_W - 100.0f, 8.0f,
                          progress, CLR_SYNC, CLR_NEUTRAL_200);
    }
}

void TitleListScreen::draw_row(C2D_TextBuf buf, size_t i,
                                float x, float y, float w, float h, bool focused) {
    (void)focused;
    float icon_x = x + (float)SP_SM;
    float icon_y = y + (h - ICON_SZ) / 2.0f;

    // Icon
    if (i < icon_cache_.size() && !titles_[i].icon.empty()) {
        if (!icon_cache_[i]) {
            icon_cache_[i] = smdh_icon_to_image(titles_[i].icon.data());
            printf(icon_cache_[i]
                   ? "[ui] built icon tex row=%zu\n"
                   : "[ui] icon tex build failed row=%zu\n", i);
        }
        if (icon_cache_[i])
            draw_image(icon_cache_[i]->img, icon_x, icon_y, ICON_SZ, ICON_SZ);
        else
            draw_rounded_rect(icon_x, icon_y, 0.51f, ICON_SZ, ICON_SZ,
                              RAD_SM, CLR_NEUTRAL_200);
    } else {
        draw_rounded_rect(icon_x, icon_y, 0.51f, ICON_SZ, ICON_SZ,
                          RAD_SM, CLR_NEUTRAL_200);
    }

    // Name with UTF-8-safe truncation
    float text_x     = icon_x + ICON_SZ + (float)SP_MD;
    float max_text_w = x + w - text_x - (float)SP_MD;
    float text_y     = y + (h - text_height(buf, TEXT_BASE, "A")) / 2.0f;

    std::string display = titles_[i].name;
    if (text_width(buf, TEXT_BASE, display.c_str()) > max_text_w) {
        while (!display.empty() &&
               text_width(buf, TEXT_BASE, (display + "...").c_str()) > max_text_w) {
            display.resize(display.size() - 1);
            while (!display.empty() &&
                   (static_cast<unsigned char>(display.back()) & 0xC0) == 0x80)
                display.resize(display.size() - 1);
        }
        display += "...";
    }
    draw_text(buf, text_x, text_y, 0.51f, TEXT_BASE, CLR_TEXT, display.c_str());
}

std::vector<Action> TitleListScreen::actions() {
    bool has_sel = !titles_.empty() && cursor_ < titles_.size();
    bool has_any = !titles_.empty();
    std::vector<Action> a;
    a.push_back({KEY_A, "A", syncing_ ? "Syncing..." : "Sync Game",
                 ACT_SYNC, !syncing_ && has_sel, ButtonStyle::PRIMARY});
    a.push_back({KEY_SELECT, "Sel", "Sync All",
                 ACT_SYNC_ALL, !syncing_ && has_any, ButtonStyle::SECONDARY});
    a.push_back({KEY_X, "X", "Conflicts",
                 ACT_CONFLICTS, true, ButtonStyle::SECONDARY});
    a.push_back({KEY_Y, "Y", "Settings",
                 ACT_SETTINGS, true, ButtonStyle::SECONDARY});
    a.push_back({KEY_L, "L", "Snapshots",
                 ACT_SNAPSHOTS, has_sel, ButtonStyle::SECONDARY});
    a.push_back({KEY_R, "R", "History",
                 ACT_HISTORY, has_sel, ButtonStyle::SECONDARY});
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
    if (syncing_) return;
    if (titles.empty()) { status_text_ = "No titles to sync"; return; }
    syncing_ = true;
    status_text_ = "Starting sync...";
    printf("[sync] starting sync for %zu title(s)\n", titles.size());
    if (worker_) { worker_->join(); delete worker_; }
    worker_ = new SyncWorker(session_->vault, session_->device_id,
                             session_->dav.as_cfg(), titles);
    worker_->start();
}
