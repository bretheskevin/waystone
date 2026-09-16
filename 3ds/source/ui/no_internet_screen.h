#pragma once
#include "screen.h"
#include "widgets.h"
#include "net_status.h"
#include <functional>
#include <string>

class NoInternetScreen : public Screen {
public:
    // on_success: called after a successful Retry; if null the screen just pops.
    NoInternetScreen(NoInternetReason reason,
                     std::function<void()> on_success = std::function<void()>());
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    // no poll() — no background worker
private:
    NoInternetReason      reason_;
    std::function<void()> on_success_;
    std::string           base_message_; // owns the joined message text
    std::string           message_;      // displayed; gains a suffix on retry failure
    Rect                  retry_rect_;   // updated each frame by draw_bottom for touch hit-test
    NoInternetScreen(const NoInternetScreen&);
    NoInternetScreen& operator=(const NoInternetScreen&);
};
