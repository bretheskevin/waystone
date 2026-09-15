#pragma once
#include "screen.h"
#include "session.h"
#include <string>

class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(Session* session);
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
private:
    Session* session_;
    size_t cursor_;
    std::string status_text_;
    static const size_t NUM_ROWS = 6;
    SettingsScreen(const SettingsScreen&);
    SettingsScreen& operator=(const SettingsScreen&);
};
