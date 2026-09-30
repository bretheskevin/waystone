#include "snapshot_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include "snapshot_browse.h"
#include <cstdio>

SnapshotScreen::SnapshotScreen(Session* session, TitleInfo selected)
    : session_(session),
      title_(selected),
      worker_(0),
      phase_(BrowsePhase::Idle)
{
    worker_ = new SnapshotWorker(title_);
    worker_->start_scan();
    printf("[snapshot] scan started for %s\n", title_.name.c_str());
}

SnapshotScreen::~SnapshotScreen() {
    if (worker_) { worker_->join(); delete worker_; }
}

void SnapshotScreen::poll() {
    if (!worker_) return;
    phase_ = worker_->phase();
    status_text_ = worker_->status();
    if (phase_ == BrowsePhase::Ready || phase_ == BrowsePhase::Done) {
        items_ = worker_->snapshots();
        if (items_.empty()) {
            cursor_ = 0;
            scroll_offset_ = 0;
        } else if (cursor_ >= items_.size()) {
            cursor_ = items_.size() - 1;
        }
    }
}

std::string SnapshotScreen::subtitle() {
    return title_.name;
}

void SnapshotScreen::browse_start_restore(size_t index) {
    worker_->start_restore(index);
}

void SnapshotScreen::draw_row(C2D_TextBuf buf, size_t i,
                               float x, float y, float w, float h, bool focused) {
    (void)focused; (void)h;
    std::string ts = human_timestamp(items_[i].timestamp);
    draw_text(buf, x + (float)SP_MD, y + (float)SP_SM, 0.51f,
              TEXT_BASE, CLR_TEXT, ts.c_str());
    std::string sz = human_size(items_[i].total_bytes);
    float sw = text_width(buf, TEXT_SM, sz.c_str());
    draw_text(buf, x + w - sw - (float)SP_MD,
              y + (float)SP_SM + 2.0f,
              0.51f, TEXT_SM, CLR_TEXT_HINT, sz.c_str());
}

void SnapshotScreen::draw_selected_detail(C2D_TextBuf buf,
                                           float area_y, float area_h) {
    (void)area_h;
    if (items_.empty() || cursor_ >= items_.size()) {
        const char* msg = "No snapshots yet for this game.";
        if (phase_ == BrowsePhase::Scanning) msg = "Scanning...";
        draw_text_centered(buf, 0, area_y + 20.0f, 0.5f, TEXT_BASE,
                           CLR_TEXT_HINT, msg, (float)SCREEN_BOT_W);
        return;
    }

    const SnapshotEntry& se = items_[cursor_];
    float y = area_y + (float)SP_SM;

    draw_text_centered_fit(buf, 0, y, 0.5f, TEXT_LG, CLR_TEXT,
                           human_timestamp(se.timestamp).c_str(),
                           (float)SCREEN_BOT_W, TEXT_BASE);
    y += 18.0f;

    char detail[128];
    std::string sz = human_size(se.total_bytes);
    snprintf(detail, sizeof(detail), "%zu file(s), %s",
             se.file_count, sz.c_str());
    draw_text_centered(buf, 0, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       detail, (float)SCREEN_BOT_W);
    y += 14.0f;

    draw_text_centered_fit(buf, 0, y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                           se.path.c_str(),
                           (float)SCREEN_BOT_W, TEXT_SM * 0.6f);
}
