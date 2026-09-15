#include "conflict_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

static const float CBTN_W   = 120.0f;
static const float CBTN_H   = 28.0f;
static const float CBTN_GAP = 16.0f;
static const float CBTN_Y   = (float)SCREEN_BOT_H - 70.0f;
static const float CBTN_TOTAL_W = CBTN_W * 2.0f + CBTN_GAP;
static const float CBTN_X1  = ((float)SCREEN_BOT_W - CBTN_TOTAL_W) / 2.0f;
static const float CBTN_X2  = CBTN_X1 + CBTN_W + CBTN_GAP;

ConflictScreen::ConflictScreen(Session* session, std::vector<TitleInfo> titles)
    : session_(session),
      worker_(0),
      cursor_(0),
      scroll_offset_(0),
      confirm_remote_(false),
      phase_(ConflictPhase::Idle)
{
    worker_ = new ConflictWorker(session_->vault, session_->device_id,
                                 session_->dav.as_cfg(), titles);
    worker_->start_scan();
}

ConflictScreen::~ConflictScreen() {
    if (worker_) { worker_->join(); delete worker_; }
}

void ConflictScreen::clamp_cursor() {
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

void ConflictScreen::poll() {
    if (!worker_) return;
    phase_ = worker_->phase();
    status_text_ = worker_->status();
    if (phase_ == ConflictPhase::Ready || phase_ == ConflictPhase::Done) {
        items_ = worker_->conflicts();
        clamp_cursor();
    }
}

void ConflictScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();

    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       "Waystone", (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_WHITE,
                       "Conflict Inbox", (float)SCREEN_TOP_W);

    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%zu conflict(s)", items_.size());
    draw_text_centered(buf, 0, 55.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       count_buf, (float)SCREEN_TOP_W);

    if (!items_.empty() && cursor_ < items_.size()) {
        const ConflictItem& ci = items_[cursor_];
        float y = 80.0f;
        float x = 20.0f;

        draw_text(buf, x, y, 0.5f, TEXT_LG, CLR_WHITE, ci.title_name.c_str());
        y += 20.0f;

        draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400, ci.group_key.c_str());
        y += 16.0f;

        char local_line[128];
        snprintf(local_line, sizeof(local_line), "Local   %.12s",
                 ci.local_hash.empty() ? "(none)" : ci.local_hash.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_BASE, CLR_SUCCESS, local_line);
        y += 16.0f;

        char remote_line[128];
        snprintf(remote_line, sizeof(remote_line), "Remote  %.12s  dev:%s",
                 ci.remote_hash.empty() ? "(none)" : ci.remote_hash.c_str(),
                 ci.remote_device_id.empty() ? "?" : ci.remote_device_id.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_BASE, CLR_SYNC, remote_line);
        y += 16.0f;

        if (!ci.remote_mtime.empty()) {
            char mtime_line[128];
            snprintf(mtime_line, sizeof(mtime_line), "Remote mtime: %s",
                     ci.remote_mtime.c_str());
            draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400, mtime_line);
        }
    }

    if (confirm_remote_) {
        float banner_y = 190.0f;
        C2D_DrawRectSolid(0, banner_y, 0.6f, (float)SCREEN_TOP_W, 40.0f, CLR_WARNING);
        draw_text_centered(buf, 0, banner_y + 4.0f, 0.61f, TEXT_BASE, CLR_NEUTRAL_900,
                           "Overwrite local with remote?", (float)SCREEN_TOP_W);
        draw_text_centered(buf, 0, banner_y + 22.0f, 0.61f, TEXT_SM, CLR_NEUTRAL_800,
                           "A: Confirm   B: Cancel", (float)SCREEN_TOP_W);
    }

    if (!confirm_remote_ && !status_text_.empty()) {
        u32 status_clr = CLR_NEUTRAL_400;
        if (phase_ == ConflictPhase::Error) status_clr = CLR_ERROR;
        else if (phase_ == ConflictPhase::Done) status_clr = CLR_SUCCESS;
        else if (phase_ == ConflictPhase::Scanning || phase_ == ConflictPhase::Resolving)
            status_clr = CLR_SYNC;
        draw_text_centered(buf, 0, 220.0f, 0.5f, TEXT_SM, status_clr,
                           status_text_.c_str(), (float)SCREEN_TOP_W);
    }
}

void ConflictScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();

    float x = (float)SP_MD;
    float w = (float)SCREEN_BOT_W - 2.0f * (float)SP_MD;
    float row_h = 28.0f;
    float y = (float)SP_MD;

    if (items_.empty()) {
        const char* msg = "No conflicts";
        if (phase_ == ConflictPhase::Scanning) msg = "Scanning...";
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
                      TEXT_BASE, CLR_TEXT, items_[i].title_name.c_str());

            char hash_cmp[32];
            snprintf(hash_cmp, sizeof(hash_cmp), "%.6s/%.6s",
                     items_[i].local_hash.c_str(), items_[i].remote_hash.c_str());
            float hw = text_width(buf, TEXT_SM, hash_cmp);
            draw_text(buf, x + w - hw - (float)SP_MD, y + (float)SP_SM + 2.0f,
                      0.51f, TEXT_SM, CLR_TEXT_HINT, hash_cmp);

            y += row_h + (float)SP_XS;
        }

        if (items_.size() > VISIBLE_ROWS) {
            char scroll_text[32];
            snprintf(scroll_text, sizeof(scroll_text), "%zu-%zu of %zu",
                     scroll_offset_ + 1, end, items_.size());
            draw_text_centered(buf, 0, y + 2.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT,
                               scroll_text, (float)SCREEN_BOT_W);
        }
    }

    bool can_act = (phase_ == ConflictPhase::Ready && !items_.empty() && !confirm_remote_);
    draw_button(buf, CBTN_X1, CBTN_Y, CBTN_W, CBTN_H, "Keep Local",
                ButtonStyle::PRIMARY, can_act);
    draw_button(buf, CBTN_X2, CBTN_Y, CBTN_W, CBTN_H, "Keep Remote",
                ButtonStyle::SECONDARY, can_act);

    if (confirm_remote_) {
        draw_footer_hint(buf, "A: Confirm  B: Cancel");
    } else {
        draw_footer_hint(buf, "A: Keep Local  X: Keep Remote  B: Back");
    }
}

void ConflictScreen::handle_input(u32 kDown, touchPosition touch) {
    if (confirm_remote_) {
        if (kDown & KEY_A) {
            worker_->resolve_keep_remote(cursor_);
            confirm_remote_ = false;
            return;
        }
        if (kDown & KEY_B) {
            confirm_remote_ = false;
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

    if (phase_ != ConflictPhase::Ready || items_.empty()) return;

    if (kDown & KEY_A) {
        worker_->resolve_keep_local(cursor_);
        return;
    }

    if (kDown & KEY_X) {
        confirm_remote_ = true;
        return;
    }

    if (touch.px != 0 || touch.py != 0) {
        Rect keep_local_btn = {CBTN_X1, CBTN_Y, CBTN_W, CBTN_H};
        Rect keep_remote_btn = {CBTN_X2, CBTN_Y, CBTN_W, CBTN_H};

        if (keep_local_btn.contains((float)touch.px, (float)touch.py)) {
            worker_->resolve_keep_local(cursor_);
            return;
        }
        if (keep_remote_btn.contains((float)touch.px, (float)touch.py)) {
            confirm_remote_ = true;
            return;
        }
    }
}
