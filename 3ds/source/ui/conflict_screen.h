#pragma once
#include "screen.h"
#include "session.h"
#include "conflict_worker.h"
#include <vector>
#include <string>

class ConflictScreen : public Screen {
public:
    ConflictScreen(Session* session, std::vector<TitleInfo> titles);
    ~ConflictScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    ConflictWorker* worker_;
    std::vector<ConflictItem> items_;
    size_t cursor_;
    size_t scroll_offset_;
    static const size_t VISIBLE_ROWS = 5;
    bool confirm_remote_;
    std::string status_text_;
    ConflictPhase phase_;

    void clamp_cursor();

    ConflictScreen(const ConflictScreen&);
    ConflictScreen& operator=(const ConflictScreen&);
};
