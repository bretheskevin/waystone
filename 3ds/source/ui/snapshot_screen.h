#pragma once
#include "screen.h"
#include "session.h"
#include "snapshot_worker.h"
#include <vector>
#include <string>

class SnapshotScreen : public Screen {
public:
    SnapshotScreen(Session* session, TitleInfo selected);
    ~SnapshotScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    TitleInfo title_;
    SnapshotWorker* worker_;
    std::vector<SnapshotEntry> items_;
    size_t cursor_;
    size_t scroll_offset_;
    static const size_t VISIBLE_ROWS = 5;
    bool confirm_restore_;
    std::string status_text_;
    SnapshotPhase phase_;

    void clamp_cursor();

    SnapshotScreen(const SnapshotScreen&);
    SnapshotScreen& operator=(const SnapshotScreen&);
};
