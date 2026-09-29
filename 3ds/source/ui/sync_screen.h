#pragma once
#include "screen.h"
#include "session.h"
#include "sync_worker.h"
#include "saves.h"
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

    bool finished() const;        // worker reached Done or Error

    SyncScreen(const SyncScreen&);
    SyncScreen& operator=(const SyncScreen&);
};
