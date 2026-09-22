#include "history_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

HistoryScreen::HistoryScreen(Session* session, TitleInfo selected)
    : session_(session),
      title_(selected),
      worker_(0),
      phase_(BrowsePhase::Idle)
{
    worker_ = new HistoryWorker(session_, title_);
    worker_->start_scan();
    printf("[history] scan started for %s\n", title_.name.c_str());
}

HistoryScreen::~HistoryScreen() {
    if (worker_) { worker_->join(); delete worker_; }
}

void HistoryScreen::poll() {
    if (!worker_) return;
    BrowsePhase prev = phase_;
    phase_ = worker_->phase();
    status_text_ = worker_->status();
    if (phase_ == BrowsePhase::Ready || phase_ == BrowsePhase::Done) {
        items_ = worker_->entries();
        if (items_.empty()) {
            cursor_ = 0;
            scroll_offset_ = 0;
        } else if (cursor_ >= items_.size()) {
            cursor_ = items_.size() - 1;
        }
    }
    if (phase_ != prev) {
        printf("[history] phase -> %d, items=%zu\n",
               (int)phase_, items_.size());
    }
}

std::string HistoryScreen::subtitle() {
    return title_.name;
}

void HistoryScreen::browse_start_restore(size_t index) {
    worker_->start_restore(index);
}

void HistoryScreen::draw_row(C2D_TextBuf buf, size_t i,
                              float x, float y, float w, float h, bool focused) {
    (void)focused; (void)h;
    draw_text(buf, x + (float)SP_MD, y + (float)SP_SM, 0.51f,
              TEXT_BASE, CLR_TEXT, items_[i].timestamp.c_str());
    float dw = text_width(buf, TEXT_SM, items_[i].device_id.c_str());
    draw_text(buf, x + w - dw - (float)SP_MD,
              y + (float)SP_SM + 2.0f,
              0.51f, TEXT_SM, CLR_TEXT_HINT,
              items_[i].device_id.c_str());
}

void HistoryScreen::draw_selected_detail(C2D_TextBuf buf,
                                          float area_y, float area_h) {
    (void)area_h;
    if (items_.empty() || cursor_ >= items_.size()) {
        const char* msg = "No history yet for this game.";
        if (phase_ == BrowsePhase::Scanning) msg = "Scanning...";
        draw_text_centered(buf, 0, area_y + 20.0f, 0.5f, TEXT_BASE,
                           CLR_TEXT_HINT, msg, (float)SCREEN_BOT_W);
        return;
    }

    const HistoryEntry& he = items_[cursor_];
    float y = area_y + (float)SP_SM;

    draw_text_centered_fit(buf, 0, y, 0.5f, TEXT_LG, CLR_TEXT,
                           he.timestamp.c_str(),
                           (float)SCREEN_BOT_W, TEXT_BASE);
    y += 18.0f;

    char detail[128];
    snprintf(detail, sizeof(detail), "Device: %s", he.device_id.c_str());
    draw_text_centered(buf, 0, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       detail, (float)SCREEN_BOT_W);
    y += 14.0f;

    std::string hash_short = he.hash.size() > 8
        ? he.hash.substr(0, 8) : he.hash;
    snprintf(detail, sizeof(detail), "Hash: %s", hash_short.c_str());
    draw_text_centered(buf, 0, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       detail, (float)SCREEN_BOT_W);
}
