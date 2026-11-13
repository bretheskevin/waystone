#pragma once
#include "screen.h"
#include "session.h"
#include "update_flow.h"
#include "widgets.h"
#include <string>

class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(Session* session);
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    UpdateFlow update_;
    size_t cursor_;
    std::string status_text_;
    static const size_t NUM_ROWS = 8;
    static const size_t FIRST_ACTION_ROW = 5;  // rows below the device line: update, save, log out
    static const size_t ROW_LOG_OUT = 7;
    Rect row_rects_[NUM_ROWS];  // bottom-screen hit boxes, refreshed every draw
    void activate_row(size_t row);  // may delete this screen (log out); caller must return
    SettingsScreen(const SettingsScreen&);
    SettingsScreen& operator=(const SettingsScreen&);
};
