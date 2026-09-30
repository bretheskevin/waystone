#pragma once
#include "screen.h"
#include <citro2d.h>
#include <vector>
#include <functional>

class App {
public:
    App();
    ~App();
    void run();
    void set_screen(Screen* s);
    void push_screen(Screen* s);
    void pop_screen();
    // Register a background reaper polled once per frame; when it returns true
    // it is dropped. Lets a screen hand off a still-running worker so pop is
    // instant instead of blocking the render thread on join(). See reap_worker.
    void defer_reap(const std::function<bool()>& fn);
    void quit();
    C2D_TextBuf text_buf() const { return text_buf_; }
    C3D_RenderTarget* top_target() const { return top_target_; }
    C3D_RenderTarget* bot_target() const { return bot_target_; }
    static App& instance();
private:
    App(const App&);
    App& operator=(const App&);
    C3D_RenderTarget* top_target_;
    C3D_RenderTarget* bot_target_;
    C2D_TextBuf text_buf_;
    std::vector<Screen*> stack_;
    std::vector<std::function<bool()>> reapers_;
    bool running_;
    bool screen_changed_;
    static App* instance_;
};

// Cancel a running worker and hand it to App::defer_reap so it is deleted once
// its thread stops. Call before pop/set_screen so the screen destructor never
// joins a live worker on the render thread. Nulls the pointer. Requires W to
// have request_cancel() and is_running() (ConflictWorker, UpdateWorker, ...).
template <typename W>
void reap_worker(W*& worker) {
    if (!worker) return;
    W* w = worker;
    worker = 0;
    w->request_cancel();
    App::instance().defer_reap([w]() {
        if (w->is_running()) return false;
        delete w;
        return true;
    });
}
