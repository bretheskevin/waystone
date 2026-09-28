#pragma once
#include "screen.h"
#include "session.h"
#include "saves.h"
#include <3ds.h>
#include <atomic>
#include <string>
#include <vector>

struct LoadResult {
    bool                   success;
    std::string            error;
    std::vector<TitleInfo> titles;
    LoadResult() : success(false) {}
};

// Worker-visible shared state (value member — no heap alloc).
struct LoadCtx {
    LoadResult        result;
    std::atomic<bool> done;
    LoadCtx() : done(false) {}
private:
    LoadCtx(const LoadCtx&);
    LoadCtx& operator=(const LoadCtx&);
};

class LoadingScreen : public Screen {
public:
    // keys_data / keys_len are borrowed (not freed) for the screen's lifetime.
    LoadingScreen(Session* session, bool auto_unlock,
                  uint8_t* keys_data = 0, size_t keys_len = 0);
    ~LoadingScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session*  session_;
    bool      auto_unlock_;
    uint8_t*  keys_data_;
    size_t    keys_len_;
    LoadCtx   ctx_;
    Thread    worker_thread_;
    bool      worker_started_;   // worker spawned yet? (deferred until 1 frame rendered)
    bool      frame_ready_;      // has poll() let at least one frame render first?
    bool      handled_;
    float     spinner_angle_;
    void on_done();
    static void worker_entry(void* arg);
    LoadingScreen(const LoadingScreen&);
    LoadingScreen& operator=(const LoadingScreen&);
};
