#include "conflict_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

ConflictScreen::ConflictScreen(Session* session, std::vector<TitleInfo> titles)
    : session_(session),
      worker_(0),
      confirm_remote_(false),
      confirm_id_(0),
      phase_(ConflictPhase::Idle),
      seen_version_(0)
{
    worker_ = new ConflictWorker(session_->vault, session_->device_id,
                                 session_->dav.as_cfg(), titles, session_->config);
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
    // Read the version BEFORE copying: a change racing the copy just triggers one more copy next frame.
    u32 v = worker_->version();
    if (v == seen_version_) return;

    u32 focused_id = (cursor_ < items_.size()) ? items_[cursor_].id : 0;
    items_ = worker_->views();
    seen_version_ = v;
    printf("[conflict] list v=%lu: %zu item(s)\n", (unsigned long)v, items_.size());

    if (items_.empty()) {
        cursor_ = 0;
        scroll_offset_ = 0;
        return;
    }
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i].id == focused_id) { cursor_ = i; break; }
    }
    if (cursor_ >= items_.size()) cursor_ = items_.size() - 1;
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
    draw_text(buf, x + (float)SP_MD, y + (float)SP_SM, 0.51f,
              TEXT_BASE, CLR_TEXT, items_[i].title_name.c_str());
    if (items_[i].queued) {
        const char* q = "Queued";
        float qw = text_width(buf, TEXT_SM, q);
        draw_text(buf, x + w - qw - (float)SP_MD, y + (float)SP_SM + 2.0f,
                  0.51f, TEXT_SM, CLR_SYNC, q);
        return;
    }
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
    if (confirm_remote_) {
        draw_confirm_banner(buf, area_y, area_h,
                            "Overwrite local with remote?",
                            ws_confirm_cancel_hint().c_str(),
                            0,
                            44.0f);
        return;
    }

    if (items_.empty() || cursor_ >= items_.size()) {
        const char* msg = "No conflicts";
        if (phase_ == ConflictPhase::Scanning) msg = "Scanning...";
        draw_text_centered(buf, 0, area_y + 20.0f, 0.5f, TEXT_BASE,
                           CLR_TEXT_HINT, msg, (float)SCREEN_BOT_W);
        return;
    }

    const ConflictView& ci = items_[cursor_];
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

    if (ci.queued) {
        draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_SYNC,
                  "Queued: runs after the current title");
    } else if (!ci.remote_mtime.empty()) {
        char mtime_line[128];
        snprintf(mtime_line, sizeof(mtime_line), "Remote mtime: %s",
                 ci.remote_mtime.c_str());
        draw_text(buf, x, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400, mtime_line);
    }
}

std::vector<Action> ConflictScreen::actions() {
    std::vector<Action> a;
    if (confirm_remote_) {
        a.push_back(make_action(WsAction::Confirm, ACT_CONFIRM, true, ButtonStyle::PRIMARY));
    } else {
        bool focus_ok = cursor_ < items_.size() && !items_[cursor_].queued;
        bool can_act = (phase_ == ConflictPhase::Ready ||
                        phase_ == ConflictPhase::Scanning) && focus_ok;
        a.push_back(make_action(WsAction::KeepLocal,  ACT_KEEP_LOCAL,  can_act, ButtonStyle::PRIMARY));
        a.push_back(make_action(WsAction::KeepRemote, ACT_KEEP_REMOTE, can_act, ButtonStyle::SECONDARY));
    }
    return a;
}

void ConflictScreen::on_action(int id) {
    switch (id) {
    case ACT_KEEP_LOCAL:
        if (cursor_ >= items_.size()) break;
        printf("[conflict] keep local id=%lu idx=%zu\n",
               (unsigned long)items_[cursor_].id, cursor_);
        worker_->resolve_keep_local(items_[cursor_].id);
        break;
    case ACT_KEEP_REMOTE:
        if (cursor_ >= items_.size()) break;
        confirm_id_ = items_[cursor_].id;
        printf("[conflict] entering confirm for id=%lu idx=%zu\n",
               (unsigned long)confirm_id_, cursor_);
        confirm_remote_ = true;
        break;
    case ACT_CONFIRM:
        printf("[conflict] confirmed keep remote id=%lu\n", (unsigned long)confirm_id_);
        worker_->resolve_keep_remote(confirm_id_);
        confirm_remote_ = false;
        confirm_id_ = 0;
        break;
    }
}

void ConflictScreen::on_back() {
    if (confirm_remote_) {
        printf("[conflict] confirm cancelled id=%lu\n", (unsigned long)confirm_id_);
        confirm_remote_ = false;
        confirm_id_ = 0;
        return;
    }

    // Detach the worker so B pops instantly: ~ConflictScreen() would otherwise
    // join() on the render thread. request_cancel() also stops queue draining.
    reap_worker(worker_);
    printf("[conflict] popping screen\n");
    App::instance().pop_screen();
}
