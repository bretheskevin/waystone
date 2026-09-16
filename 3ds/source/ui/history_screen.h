#pragma once
#include "screen.h"
#include "session.h"
#include "history_worker.h"
#include <vector>
#include <string>

class HistoryScreen : public Screen {
public:
    HistoryScreen(Session* session, TitleInfo selected);
    ~HistoryScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    TitleInfo title_;
    HistoryWorker* worker_;
    std::vector<HistoryEntry> items_;
    size_t cursor_;
    size_t scroll_offset_;
    static const size_t VISIBLE_ROWS = 5;
    bool confirm_restore_;
    std::string status_text_;
    BrowsePhase phase_;

    void clamp_cursor();

    HistoryScreen(const HistoryScreen&);
    HistoryScreen& operator=(const HistoryScreen&);
};
