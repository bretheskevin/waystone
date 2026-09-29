#include "sync_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include <cstdio>

SyncScreen::SyncScreen(Session* session, std::vector<TitleInfo> titles)
    : session_(session),
      worker_(session->vault, session->device_id, session->dav.as_cfg(), titles),
      worker_started_(false), frame_ready_(false), spinner_angle_(0.0f)
{
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

void SyncScreen::draw_top(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    SyncPhase ph = worker_.phase();

    if (ph != SyncPhase::Done && ph != SyncPhase::Error) {
        draw_spinner(buf, SCREEN_TOP_W / 2.0f, 80.0f, spinner_angle_);
    }

    u32 status_color = CLR_SYNC;
    if (ph == SyncPhase::Error)      status_color = CLR_ERROR;
    else if (ph == SyncPhase::Done)  status_color = CLR_SUCCESS;

    std::string status = worker_.status();
    draw_text_centered(buf, 0, 130.0f, 0.5f, TEXT_BASE, status_color,
                       status.c_str(), (float)SCREEN_TOP_W);

    if (ph == SyncPhase::Running) {
        int total = worker_.total_count();
        int done  = worker_.pushed_count() + worker_.restored_count();
        float progress = (total > 0) ? (float)done / (float)(total * 2) : 0.0f;
        char prog_text[64];
        snprintf(prog_text, sizeof(prog_text), "Pushed: %d  Restored: %d",
                 worker_.pushed_count(), worker_.restored_count());
        draw_text_centered(buf, 0, 150.0f, 0.5f, TEXT_SM,
                           CLR_NEUTRAL_400, prog_text, (float)SCREEN_TOP_W);
        draw_progress_bar(50.0f, 168.0f,
                          (float)SCREEN_TOP_W - 100.0f, 8.0f,
                          progress, CLR_SYNC, CLR_NEUTRAL_200);
    }
}

void SyncScreen::draw_bottom(C3D_RenderTarget* /*target*/) {
    draw_footer_hint(App::instance().text_buf(),
                     finished() ? "Press A to continue" : "Syncing... please wait");
}

void SyncScreen::handle_input(u32 kDown, touchPosition /*touch*/) {
    // Fully modal: swallow all input until the sync finishes.
    if (!finished()) return;
    if (kDown & (KEY_A | KEY_B)) {
        printf("[sync] modal loader dismissed by user\n");
        App::instance().pop_screen();  // deletes this
    }
}

void SyncScreen::poll() {
    spinner_angle_ += 0.05f;

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
}
