#include "history_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

static const float RBTN_W   = 160.0f;
static const float RBTN_H   = 28.0f;
static const float RBTN_Y   = (float)SCREEN_BOT_H - 70.0f;
static const float RBTN_X   = ((float)SCREEN_BOT_W - RBTN_W) / 2.0f;

HistoryScreen::HistoryScreen(Session* session, TitleInfo selected)
    : session_(session),
      title_(selected),
      worker_(0),
      cursor_(0),
      scroll_offset_(0),
      confirm_restore_(false),
      phase_(BrowsePhase::Idle)
{
    worker_ = new HistoryWorker(session_, title_);
    worker_->start_scan();
}

HistoryScreen::~HistoryScreen() {
    if (worker_) { worker_->join(); delete worker_; }
}

void HistoryScreen::clamp_cursor() {
    if (items_.empty()) {
        cursor_ = 0;
        scroll_offset_ = 0;
    } else {
        if (cursor_ >= items_.size()) cursor_ = items_.size() - 1;
        if (cursor_ < scroll_offset_) scroll_offset_ = cursor_;
        else if (cursor_ >= scroll_offset_ + VISIBLE_ROWS)
            scroll_offset_ = cursor_ - VISIBLE_ROWS + 1;
    }
}

void HistoryScreen::poll() {
    if (!worker_) return;
    phase_ = worker_->phase();
    status_text_ = worker_->status();
    if (phase_ == BrowsePhase::Ready || phase_ == BrowsePhase::Done) {
        items_ = worker_->entries();
        clamp_cursor();
    }
}

void HistoryScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();

    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       "Waystone", (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_WHITE,
                       "History", (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 55.0f, 0.5f, TEXT_BASE, CLR_NEUTRAL_400,
                       title_.name.c_str(), (float)SCREEN_TOP_W);

    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%zu version(s)", items_.size());
    draw_text_centered(buf, 0, 75.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       count_buf, (float)SCREEN_TOP_W);

    if (!items_.empty() && cursor_ < items_.size()) {
        const HistoryEntry& he = items_[cursor_];
        float y = 100.0f;
        float x = 20.0f;

        draw_text(buf, x, y, 0.5f, TEXT_LG, CLR_WHITE,
                  he.timestamp.c_str());
        y += 20.0f;

        char detail[128];
        snprintf(detail, sizeof(detail), "Device: %s", he.device_id.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_BASE, CLR_NEUTRAL_400, detail);
        y += 16.0f;

        std::string hash_short = he.hash.size() > 8 ? he.hash.substr(0, 8) : he.hash;
        snprintf(detail, sizeof(detail), "Hash: %s", hash_short.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400, detail);
    }

    if (confirm_restore_) {
        float banner_y = 175.0f;
        C2D_DrawRectSolid(0, banner_y, 0.6f, (float)SCREEN_TOP_W, 50.0f,
                          CLR_WARNING);
        draw_text_centered(buf, 0, banner_y + 4.0f, 0.61f, TEXT_BASE,
                           CLR_NEUTRAL_900,
                           "Restore this version?",
                           (float)SCREEN_TOP_W);
        draw_text_centered(buf, 0, banner_y + 20.0f, 0.61f, TEXT_SM,
                           CLR_NEUTRAL_800,
                           "Current save will be backed up first.",
                           (float)SCREEN_TOP_W);
        draw_text_centered(buf, 0, banner_y + 36.0f, 0.61f, TEXT_SM,
                           CLR_NEUTRAL_800,
                           "A: Confirm   B: Cancel",
                           (float)SCREEN_TOP_W);
    }

    if (!confirm_restore_ && !status_text_.empty()) {
        u32 status_clr = CLR_NEUTRAL_400;
        if (phase_ == BrowsePhase::Error) status_clr = CLR_ERROR;
        else if (phase_ == BrowsePhase::Done) status_clr = CLR_SUCCESS;
        else if (phase_ == BrowsePhase::Scanning ||
                 phase_ == BrowsePhase::Restoring)
            status_clr = CLR_SYNC;
        draw_text_centered(buf, 0, 220.0f, 0.5f, TEXT_SM, status_clr,
                           status_text_.c_str(), (float)SCREEN_TOP_W);
    }
}

void HistoryScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();

    float x = (float)SP_MD;
    float w = (float)SCREEN_BOT_W - 2.0f * (float)SP_MD;
    float row_h = 28.0f;
    float y = (float)SP_MD;

    if (items_.empty()) {
        const char* msg = "No history yet for this game.";
        if (phase_ == BrowsePhase::Scanning) msg = "Scanning...";
        draw_text_centered(buf, 0, 60.0f, 0.5f, TEXT_BASE, CLR_TEXT_HINT,
                           msg, (float)SCREEN_BOT_W);
    } else {
        size_t end = scroll_offset_ + VISIBLE_ROWS;
        if (end > items_.size()) end = items_.size();
        for (size_t i = scroll_offset_; i < end; i++) {
            bool focused = (cursor_ == i);
            u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
            if (focused) {
                draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2,
                                  RAD_SM + 1, CLR_ACCENT);
            }
            draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);

            draw_text(buf, x + (float)SP_MD, y + (float)SP_SM, 0.51f,
                      TEXT_BASE, CLR_TEXT,
                      items_[i].timestamp.c_str());

            float dw = text_width(buf, TEXT_SM, items_[i].device_id.c_str());
            draw_text(buf, x + w - dw - (float)SP_MD,
                      y + (float)SP_SM + 2.0f,
                      0.51f, TEXT_SM, CLR_TEXT_HINT,
                      items_[i].device_id.c_str());

            y += row_h + (float)SP_XS;
        }

        if (items_.size() > VISIBLE_ROWS) {
            char scroll_text[32];
            snprintf(scroll_text, sizeof(scroll_text), "%zu-%zu of %zu",
                     scroll_offset_ + 1, end, items_.size());
            draw_text_centered(buf, 0, y + 2.0f, 0.5f, TEXT_SM,
                               CLR_TEXT_HINT, scroll_text,
                               (float)SCREEN_BOT_W);
        }
    }

    bool can_restore = (phase_ == BrowsePhase::Ready &&
                        !items_.empty() && !confirm_restore_);
    draw_button(buf, RBTN_X, RBTN_Y, RBTN_W, RBTN_H, "Restore",
                ButtonStyle::PRIMARY, can_restore);

    if (confirm_restore_) {
        draw_footer_hint(buf, "A: Confirm  B: Cancel");
    } else {
        draw_footer_hint(buf, "A: Restore  B: Back  DPad: Navigate");
    }
}

void HistoryScreen::handle_input(u32 kDown, touchPosition touch) {
    if (confirm_restore_) {
        if (kDown & KEY_A) {
            worker_->start_restore(cursor_);
            confirm_restore_ = false;
            return;
        }
        if (kDown & KEY_B) {
            confirm_restore_ = false;
            return;
        }
        return;
    }

    if (kDown & KEY_B) {
        App::instance().pop_screen();
        return;
    }

    if (!items_.empty()) {
        if (kDown & KEY_DUP) {
            cursor_ = (cursor_ == 0) ? items_.size() - 1 : cursor_ - 1;
        }
        if (kDown & KEY_DDOWN) {
            cursor_ = (cursor_ + 1) % items_.size();
        }
        if (cursor_ < scroll_offset_) scroll_offset_ = cursor_;
        else if (cursor_ >= scroll_offset_ + VISIBLE_ROWS)
            scroll_offset_ = cursor_ - VISIBLE_ROWS + 1;
    }

    if (phase_ != BrowsePhase::Ready || items_.empty()) return;

    if (kDown & KEY_A) {
        confirm_restore_ = true;
        return;
    }

    if (touch.px != 0 || touch.py != 0) {
        Rect restore_btn = {RBTN_X, RBTN_Y, RBTN_W, RBTN_H};
        if (restore_btn.contains((float)touch.px, (float)touch.py)) {
            confirm_restore_ = true;
            return;
        }
    }
}
