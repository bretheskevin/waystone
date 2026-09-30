#include "conflict_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

ConflictScreen::ConflictScreen(Session* session, std::vector<TitleInfo> titles)
    : session_(session),
      worker_(0),
      confirm_remote_(false),
      phase_(ConflictPhase::Idle)
{
    worker_ = new ConflictWorker(session_->vault, session_->device_id,
                                 session_->dav.as_cfg(), titles);
    worker_->start_scan();
    printf("[conflict] scan started for %zu titles\n", titles.size());
}

ConflictScreen::~ConflictScreen() {
    // Only join if we still own the worker (not detached by on_back)
    if (worker_) { worker_->join(); delete worker_; }
}

void ConflictScreen::poll() {
    if (!worker_) return;
    phase_ = worker_->phase();
    status_text_ = worker_->status();
    if (phase_ == ConflictPhase::Ready || phase_ == ConflictPhase::Done) {
        items_ = worker_->conflicts();
        // Clamp cursor after items update
        if (items_.empty()) {
            cursor_ = 0;
            scroll_offset_ = 0;
        } else {
            if (cursor_ >= items_.size()) cursor_ = items_.size() - 1;
        }
    }
}

// ---- ListScreen hooks ----

std::string ConflictScreen::subtitle() {
    char buf[64];
    snprintf(buf, sizeof(buf), "%zu conflict(s)", items_.size());
    return std::string(buf);
}

float ConflictScreen::status_area_height() const {
    // Status line only when not in confirm mode and text is present
    if (!confirm_remote_ && !status_text_.empty()) return 18.0f;
    return 0.0f;
}

void ConflictScreen::draw_top_status(C2D_TextBuf buf, float sy) {
    if (confirm_remote_ || status_text_.empty()) return;
    u32 clr = CLR_NEUTRAL_400;
    if (phase_ == ConflictPhase::Error) clr = CLR_ERROR;
    else if (phase_ == ConflictPhase::Done) clr = CLR_SUCCESS;
    else if (phase_ == ConflictPhase::Scanning ||
             phase_ == ConflictPhase::Resolving) clr = CLR_SYNC;
    draw_text_centered(buf, 0, sy, 0.5f, TEXT_SM, clr,
                       status_text_.c_str(), (float)SCREEN_TOP_W);
}

void ConflictScreen::draw_row(C2D_TextBuf buf, size_t i,
                               float x, float y, float w, float h, bool focused) {
    (void)focused; (void)h;
    // Title name on the left
    draw_text(buf, x + (float)SP_MD, y + (float)SP_SM, 0.51f,
              TEXT_BASE, CLR_TEXT, items_[i].title_name.c_str());
    // Hash comparison on the right
    char hash_cmp[32];
    snprintf(hash_cmp, sizeof(hash_cmp), "%.6s/%.6s",
             items_[i].local_hash.c_str(),
             items_[i].remote_hash.c_str());
    float hw = text_width(buf, TEXT_SM, hash_cmp);
    draw_text(buf, x + w - hw - (float)SP_MD,
              y + (float)SP_SM + 2.0f,
              0.51f, TEXT_SM, CLR_TEXT_HINT, hash_cmp);
}

void ConflictScreen::draw_detail(C2D_TextBuf buf,
                                  float area_y, float area_h) {
    // -- Confirm banner (replaces detail when in confirm mode) --
    if (confirm_remote_) {
        draw_confirm_banner(buf, area_y, area_h,
                            "Overwrite local with remote?",
                            "A: Confirm   B: Cancel",
                            0,
                            44.0f);
        return;
    }

    // -- Selected conflict detail --
    if (items_.empty() || cursor_ >= items_.size()) {
        const char* msg = "No conflicts";
        if (phase_ == ConflictPhase::Scanning) msg = "Scanning...";
        draw_text_centered(buf, 0, area_y + 20.0f, 0.5f, TEXT_BASE,
                           CLR_TEXT_HINT, msg, (float)SCREEN_BOT_W);
        return;
    }

    const ConflictItem& ci = items_[cursor_];
    float y = area_y + (float)SP_SM;
    float x = (float)SP_MD;

    draw_text_centered_fit(buf, 0, y, 0.5f, TEXT_LG, CLR_TEXT,
                           ci.title_name.c_str(),
                           (float)SCREEN_BOT_W, TEXT_BASE);
    y += 18.0f;

    draw_text_centered_fit(buf, 0, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                           ci.group_key.c_str(),
                           (float)SCREEN_BOT_W, TEXT_SM * 0.7f);
    y += 14.0f;

    char local_line[128];
    snprintf(local_line, sizeof(local_line), "Local   %.12s",
             ci.local_hash.empty() ? "(none)" : ci.local_hash.c_str());
    draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_SUCCESS, local_line);
    y += 14.0f;

    char remote_line[128];
    snprintf(remote_line, sizeof(remote_line), "Remote  %.12s  dev:%s",
             ci.remote_hash.empty() ? "(none)" : ci.remote_hash.c_str(),
             ci.remote_device_id.empty() ? "?" : ci.remote_device_id.c_str());
    draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_SYNC, remote_line);
    y += 14.0f;

    if (!ci.remote_mtime.empty()) {
        char mtime_line[128];
        snprintf(mtime_line, sizeof(mtime_line), "Remote mtime: %s",
                 ci.remote_mtime.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400, mtime_line);
    }
}

std::vector<Action> ConflictScreen::actions() {
    std::vector<Action> a;
    if (confirm_remote_) {
        // Confirm mode: only A=Confirm (B handled by on_back via modal_active)
        a.push_back({KEY_A, "A", "Confirm",
                     ACT_CONFIRM, true, ButtonStyle::PRIMARY});
    } else {
        bool can_act = (phase_ == ConflictPhase::Ready &&
                        !items_.empty());
        a.push_back({KEY_A, "A", "Keep Local",
                     ACT_KEEP_LOCAL, can_act, ButtonStyle::PRIMARY});
        a.push_back({KEY_X, "X", "Keep Remote",
                     ACT_KEEP_REMOTE, can_act, ButtonStyle::SECONDARY});
    }
    return a;
}

void ConflictScreen::on_action(int id) {
    switch (id) {
    case ACT_KEEP_LOCAL:
        printf("[conflict] keep local idx=%zu\n", cursor_);
        worker_->resolve_keep_local(cursor_);
        break;
    case ACT_KEEP_REMOTE:
        printf("[conflict] entering confirm for idx=%zu\n", cursor_);
        confirm_remote_ = true;
        break;
    case ACT_CONFIRM:
        printf("[conflict] confirmed keep remote idx=%zu\n", cursor_);
        worker_->resolve_keep_remote(cursor_);
        confirm_remote_ = false;
        break;
    }
}

void ConflictScreen::on_back() {
    // ---- Confirm cancel ----
    if (confirm_remote_) {
        printf("[conflict] confirm cancelled\n");
        confirm_remote_ = false;
        return;
    }

    // ---- Deferred-cancel: detach worker so B pops instantly ----
    // ~ConflictScreen() would otherwise join() on the render thread,
    // freezing the UI until the in-flight title's network scan returns.
    reap_worker(worker_);
    printf("[conflict] popping screen\n");
    App::instance().pop_screen();
}
