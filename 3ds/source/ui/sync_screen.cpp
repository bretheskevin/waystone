#include "sync_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include "snapshot_browse.h" // human_size
#include <cstdio>

static const float ROW_H        = 22.0f;
static const float ROW_PITCH    = 24.0f;
static const float LIST_X       = (float)SP_MD;
static const float LIST_TOP     = (float)SP_MD;
static const float LIST_BOTTOM  = 214.0f;  // footer hint sits below this
static const float LABEL_COL_W  = 150.0f;
static const float NAME_INDENT  = 20.0f;

static u32 state_color(TitleState s) {
    switch (s) {
    case TitleState::Pending:    return CLR_NEUTRAL_400;
    case TitleState::Active:     return CLR_SYNC;
    case TitleState::Conflict:   return CLR_WARNING;
    case TitleState::Failed:     return CLR_ERROR;
    case TitleState::InSync:
    case TitleState::Uploaded:
    case TitleState::Downloaded:
    case TitleState::UpDown:     return CLR_SUCCESS;
    }
    return CLR_NEUTRAL_400;
}

SyncScreen::SyncScreen(Session* session, std::vector<TitleInfo> titles)
    : session_(session),
      worker_(session->vault, session->device_id, session->dav.as_cfg(), titles),
      worker_started_(false), frame_ready_(false), spinner_angle_(0.0f),
      name_cache_(titles.size()), scroll_(0), finished_logged_(false)
{
    results_ = worker_.results();
    printf("[sync] modal loader opened for %zu title(s) — worker deferred to first frame\n",
           titles.size());
}

SyncScreen::~SyncScreen() {
    // worker_ dtor join()s. Input is blocked until the sync finishes, so by the
    // time this screen is popped the worker has already returned.
    printf("[sync] modal loader closing\n");
}

bool SyncScreen::finished() const {
    SyncPhase ph = worker_.phase();
    return ph == SyncPhase::Done || ph == SyncPhase::Error;
}

size_t SyncScreen::visible_rows() const {
    size_t v = (size_t)((LIST_BOTTOM - LIST_TOP + (ROW_PITCH - ROW_H)) / ROW_PITCH);
    return v > 0 ? v : 1;
}

size_t SyncScreen::max_scroll() const {
    size_t n = results_.size();
    size_t vis = visible_rows();
    return (n > vis) ? n - vis : 0;
}

void SyncScreen::follow_active() {
    int cur = worker_.current_index();
    if (cur < 0) return;
    size_t c = (size_t)cur;
    size_t vis = visible_rows();
    if (c < scroll_) scroll_ = c;
    else if (c >= scroll_ + vis) scroll_ = c - vis + 1;
}

void SyncScreen::draw_top(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    const float W = (float)SCREEN_TOP_W;
    SyncPhase ph = worker_.phase();

    if (!finished()) {
        draw_spinner(buf, W / 2.0f, 36.0f, spinner_angle_);

        int idx = worker_.current_index();
        int n   = worker_.total_count();
        const char* name = (idx >= 0 && idx < n) ? worker_.titles()[(size_t)idx].name.c_str()
                                                 : "Preparing...";
        draw_text_centered_fit(buf, (float)SP_XL, 56.0f, 0.5f, TEXT_LG, CLR_TEXT, name,
                               W - 2.0f * (float)SP_XL, TEXT_SM);

        std::string step = worker_.step();
        if (!step.empty()) {
            draw_text_centered(buf, 0, 84.0f, 0.5f, TEXT_BASE, CLR_SYNC, step.c_str(), W);
        }

        size_t got = worker_.bytes_got();
        size_t total = worker_.bytes_total();
        if (got > 0 || total > 0) {
            std::string g = human_size((unsigned long long)got);
            char bytes_line[64];
            if (total > 0) {
                std::string t = human_size((unsigned long long)total);
                snprintf(bytes_line, sizeof(bytes_line), "%s / %s", g.c_str(), t.c_str());
            } else {
                snprintf(bytes_line, sizeof(bytes_line), "%s", g.c_str());
            }
            draw_text_centered(buf, 0, 104.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, bytes_line, W);
        }

        if (idx >= 0 && idx < n) {
            char pass_line[64];
            snprintf(pass_line, sizeof(pass_line), "%s pass - %d of %d",
                     worker_.current_pass() == 0 ? "Upload" : "Download", idx + 1, n);
            draw_text_centered(buf, 0, 122.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT, pass_line, W);
        }

        float p = worker_.progress();
        draw_progress_bar(50.0f, 150.0f, W - 100.0f, 8.0f, p, CLR_SYNC, CLR_NEUTRAL_200);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", (int)(p * 100.0f + 0.5f));
        draw_text_centered(buf, 0, 164.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, pct, W);
        return;
    }

    bool problems = false;
    for (size_t i = 0; i < results_.size(); i++) {
        if (results_[i].state == TitleState::Failed ||
            results_[i].state == TitleState::Conflict) {
            problems = true;
            break;
        }
    }
    u32 color = (ph == SyncPhase::Error) ? CLR_ERROR
              : (problems ? CLR_WARNING : CLR_SUCCESS);
    std::string status = worker_.status();
    draw_text_wrapped_centered(buf, (float)SP_XL, 96.0f, 0.5f, TEXT_BASE, color, status.c_str(),
                               W - 2.0f * (float)SP_XL, 18.0f);
    if (ph == SyncPhase::Done) {
        draw_progress_bar(50.0f, 150.0f, W - 100.0f, 8.0f, 1.0f, color, CLR_NEUTRAL_200);
    }
}

void SyncScreen::draw_row(C2D_TextBuf buf, size_t i, float y) {
    const TitleResult& r = results_[i];
    const float w = (float)SCREEN_BOT_W - 2.0f * LIST_X;
    u32 c = state_color(r.state);

    draw_rounded_rect(LIST_X, y, 0.5f, w, ROW_H, RAD_SM,
                      r.state == TitleState::Active ? CLR_PRIMARY_50 : CLR_CARD_BG);
    C2D_DrawCircleSolid(LIST_X + 10.0f, y + ROW_H / 2.0f, 0.51f, 4.0f, c);

    const char* label = (r.state == TitleState::Failed && !r.reason.empty())
                        ? r.reason.c_str() : title_state_label(r.state);
    float lscale = TEXT_SM;
    float lw = text_width(buf, lscale, label);
    if (lw > LABEL_COL_W) {
        lscale = TEXT_SM * (LABEL_COL_W / lw);
        lw = LABEL_COL_W;
    }
    draw_text(buf, LIST_X + w - lw - (float)SP_MD, y + 4.0f, 0.51f, lscale, c, label);

    if (name_cache_[i].empty()) {
        float name_max = w - NAME_INDENT - LABEL_COL_W - 2.0f * (float)SP_MD;
        name_cache_[i] = truncate_text_fit(buf, TEXT_SM, worker_.titles()[i].name.c_str(),
                                           name_max);
    }
    draw_text(buf, LIST_X + NAME_INDENT, y + 4.0f, 0.51f, TEXT_SM, CLR_TEXT,
              name_cache_[i].c_str());
}

void SyncScreen::draw_bottom(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    size_t n = results_.size();
    size_t vis = visible_rows();
    float y = LIST_TOP;
    for (size_t i = scroll_; i < n && i < scroll_ + vis; i++) {
        draw_row(buf, i, y);
        y += ROW_PITCH;
    }

    const char* hint = "Syncing... please wait";
    if (finished()) hint = (n > vis) ? "A: Continue  D-Pad: Scroll" : "Press A to continue";
    draw_footer_hint(buf, hint);
}

void SyncScreen::handle_input(u32 kDown, touchPosition /*touch*/) {
    // Fully modal: swallow all input until the sync finishes.
    if (!finished()) return;
    if ((kDown & KEY_DUP) && scroll_ > 0) {
        scroll_--;
        printf("[ui] sync list scroll -> %zu\n", scroll_);
    }
    if ((kDown & KEY_DDOWN) && scroll_ < max_scroll()) {
        scroll_++;
        printf("[ui] sync list scroll -> %zu\n", scroll_);
    }
    if (kDown & (KEY_A | KEY_B)) {
        printf("[sync] modal loader dismissed by user\n");
        App::instance().pop_screen();  // deletes this
        return;
    }
}

void SyncScreen::poll() {
    spinner_angle_ += 0.05f;
    if (!finished_logged_) results_ = worker_.results();

    // Defer worker start until one frame has rendered so the spinner is visible
    // before the heavy/crash-prone network + crypto work begins (mirrors LoadingScreen).
    if (!worker_started_) {
        if (!frame_ready_) {
            frame_ready_ = true;
            return;
        }
        printf("[sync] first frame rendered — starting sync worker\n");
        worker_.start();
        worker_started_ = true;
    }

    if (!finished()) {
        follow_active();
    } else if (!finished_logged_) {
        finished_logged_ = true;
        results_ = worker_.results();  // the copy above may predate the worker's final set_result
        if (scroll_ > max_scroll()) scroll_ = max_scroll();
        printf("[ui] sync screen finished: %s\n", worker_.status().c_str());
    }
}
