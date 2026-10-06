#pragma once
#include "screen.h"
#include "session.h"
#include "sync_worker.h"
#include "saves.h"
#include <string>
#include <vector>

// Modal loader shown on top of the title list while a push/pull runs.
// Owns the SyncWorker and blocks ALL input until the sync finishes, so no other
// action can fire and the heavy title-list rendering underneath is suspended
// (App only polls/draws the top of the stack). The worker still runs on its own
// thread, so the spinner keeps animating and aptMainLoop keeps pumping.
class SyncScreen : public Screen {
public:
    SyncScreen(Session* session, std::vector<TitleInfo> titles);
    ~SyncScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();

private:
    Session*    session_;
    SyncWorker  worker_;
    bool        worker_started_;  // start() deferred until one frame has rendered
    bool        frame_ready_;     // has poll() let a spinner frame render first?
    float       spinner_angle_;
    std::vector<TitleResult> results_;     // per-frame copy of worker_.results() (poll)
    std::vector<std::string> name_cache_;  // truncated row names, built lazily on render thread
    size_t      scroll_;                   // first visible row of the bottom list
    bool        finished_logged_;

    bool   finished() const;        // worker reached Done or Error
    size_t visible_rows() const;
    size_t max_scroll() const;
    void   follow_active();         // keep the active title's row visible while running
    void   draw_row(C2D_TextBuf buf, size_t i, float y);

    SyncScreen(const SyncScreen&);
    SyncScreen& operator=(const SyncScreen&);
};
