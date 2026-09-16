#include "title_list_screen.h"
#include "settings_screen.h"
#include "conflict_screen.h"
#include "snapshot_screen.h"
#include "no_internet_screen.h"
#include "net_status.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

TitleListScreen::TitleListScreen(Session* session)
    : session_(session), worker_(0), scroll_offset_(0), cursor_(0), syncing_(false) {
    printf("[title_list] enumerating titles...\n");
    titles_ = list_titles();
    printf("[title_list] found %zu titles\n", titles_.size());
    status_text_ = "Ready";
}

TitleListScreen::TitleListScreen(Session* session, const std::vector<TitleInfo>& preloaded_titles)
    : session_(session), worker_(0), scroll_offset_(0), cursor_(0), syncing_(false) {
    titles_ = preloaded_titles;
    status_text_ = "Ready";
}

TitleListScreen::~TitleListScreen() {
    if (worker_) { worker_->join(); delete worker_; }
}
void TitleListScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, "Waystone", (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_WHITE, "Your Saves", (float)SCREEN_TOP_W);
    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%zu titles installed", titles_.size());
    draw_text_centered(buf, 0, 60.0f, 0.5f, TEXT_BASE, CLR_NEUTRAL_400, count_buf, (float)SCREEN_TOP_W);
    u32 status_color = CLR_SYNC;
    if (worker_) {
        SyncPhase ph = worker_->phase();
        if (ph == SyncPhase::Error) status_color = CLR_ERROR;
        else if (ph == SyncPhase::Done) status_color = CLR_SUCCESS;
    }
    draw_text_centered(buf, 0, 100.0f, 0.5f, TEXT_BASE, status_color, status_text_.c_str(), (float)SCREEN_TOP_W);
    if (worker_ && worker_->phase() == SyncPhase::Running) {
        int total = worker_->total_count();
        int done = worker_->pushed_count() + worker_->restored_count();
        float progress = (total > 0) ? (float)done / (float)(total*2) : 0.0f;
        char prog_text[64];
        snprintf(prog_text, sizeof(prog_text), "Pushed: %d  Restored: %d", worker_->pushed_count(), worker_->restored_count());
        draw_text_centered(buf, 0, 130.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, prog_text, (float)SCREEN_TOP_W);
        draw_progress_bar(50.0f, 160.0f, (float)SCREEN_TOP_W-100.0f, 8.0f, progress, CLR_SYNC, CLR_NEUTRAL_200);
    }
}
void TitleListScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    float x = SP_MD; float w = (float)SCREEN_BOT_W - 2.0f*SP_MD; float row_h = 28.0f; float y = SP_MD;
    size_t end = scroll_offset_ + VISIBLE_ROWS;
    if (end > titles_.size()) end = titles_.size();
    for (size_t i = scroll_offset_; i < end; i++) {
        bool focused = (cursor_ == i);
        u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        if (focused) {
            draw_rounded_rect(x-1, y-1, 0.49f, w+2, row_h+2, RAD_SM+1, CLR_ACCENT);
            draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        }
        draw_text(buf, x+SP_MD, y+SP_SM, 0.51f, TEXT_BASE, CLR_TEXT, titles_[i].name.c_str());
        char uid_str[16];
        snprintf(uid_str, sizeof(uid_str), "%05X", titles_[i].unique_id);
        float uw = text_width(buf, TEXT_SM, uid_str);
        draw_text(buf, x+w-uw-SP_MD, y+SP_SM+2.0f, 0.51f, TEXT_SM, CLR_TEXT_HINT, uid_str);
        y += row_h + SP_XS;
    }
    float btn_y = (float)SCREEN_BOT_H - 50.0f;
    float btn_w = 120.0f; float btn_x = ((float)SCREEN_BOT_W - btn_w)/2.0f;
    bool btn_focused = (cursor_ == titles_.size());
    const char* btn_label = syncing_ ? "Syncing..." : "Sync All";
    draw_button(buf, btn_x, btn_y, btn_w, 28.0f, btn_label, ButtonStyle::PRIMARY, btn_focused);
    if (titles_.size() > VISIBLE_ROWS) {
        char scroll_text[32];
        snprintf(scroll_text, sizeof(scroll_text), "%zu-%zu of %zu", scroll_offset_+1, end, titles_.size());
        draw_text_centered(buf, 0, btn_y-16.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT, scroll_text, (float)SCREEN_BOT_W);
    }
    draw_footer_hint(buf, "A: Sync  X: Conflicts  Y: Settings  L: Snapshots");
}
void TitleListScreen::handle_input(u32 kDown, touchPosition touch) {
    size_t item_count = titles_.size() + 1;
    if (kDown & KEY_DUP)   { if (item_count>0) cursor_=(cursor_==0)?item_count-1:cursor_-1; }
    if (kDown & KEY_DDOWN) { if (item_count>0) cursor_=(cursor_+1)%item_count; }
    if (cursor_ < titles_.size()) {
        if (cursor_ < scroll_offset_) scroll_offset_ = cursor_;
        else if (cursor_ >= scroll_offset_ + VISIBLE_ROWS) scroll_offset_ = cursor_ - VISIBLE_ROWS + 1;
    }
    if (kDown & KEY_X) { App::instance().push_screen(new ConflictScreen(session_, titles_)); return; }
    if ((kDown & KEY_L) && cursor_ < titles_.size()) { App::instance().push_screen(new SnapshotScreen(session_, titles_[cursor_])); return; }
    if (kDown & KEY_Y) { App::instance().push_screen(new SettingsScreen(session_)); return; }
    if (kDown & KEY_A) { if (cursor_ == titles_.size() || titles_.empty()) start_sync_or_gate(); }
    if (touch.px != 0 || touch.py != 0) {
        float btn_y=(float)SCREEN_BOT_H-50.0f, btn_w=120.0f, btn_x=((float)SCREEN_BOT_W-btn_w)/2.0f;
        Rect btn = {btn_x, btn_y, btn_w, 28.0f};
        if (btn.contains((float)touch.px, (float)touch.py)) start_sync_or_gate();
    }
}
void TitleListScreen::start_sync_or_gate() {
    if (!network_available()) {
        App::instance().push_screen(
            new NoInternetScreen(NoInternetReason::NoNetwork, [this]() {
                start_sync();
            }));
        return;
    }
    start_sync();
}
void TitleListScreen::start_sync() {
    if (syncing_) return;
    if (titles_.empty()) { status_text_ = "No titles to sync"; return; }
    syncing_ = true; status_text_ = "Starting sync...";
    if (worker_) { worker_->join(); delete worker_; }
    worker_ = new SyncWorker(session_->vault, session_->device_id, session_->dav.as_cfg(), titles_);
    worker_->start();
}
void TitleListScreen::poll() {
    if (!worker_ || !syncing_) return;
    SyncPhase ph = worker_->phase();
    status_text_ = worker_->status();
    if (ph == SyncPhase::Done || ph == SyncPhase::Error) syncing_ = false;
}
