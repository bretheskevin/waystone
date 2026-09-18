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
    // instant instead of blocking the render thread on join(). See ConflictScreen.
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
