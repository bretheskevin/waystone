#pragma once
#include "screen.h"
#include "session.h"
#include "sync_worker.h"
#include "saves.h"
#include "icon_tex.h"
#include <vector>

class TitleListScreen : public Screen {
public:
    TitleListScreen(Session* session);
    TitleListScreen(Session* session, const std::vector<TitleInfo>& preloaded_titles);
    ~TitleListScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_;
    std::vector<TitleInfo> titles_;
    SyncWorker* worker_;
    size_t scroll_offset_;
    size_t cursor_;
    std::string status_text_;
    bool syncing_;
    static const size_t VISIBLE_ROWS = 3;
    std::vector<IconImage*> icon_cache_;
    void init_icon_cache();
    void start_sync();
    void start_sync_or_gate();
    TitleListScreen(const TitleListScreen&);
    TitleListScreen& operator=(const TitleListScreen&);
};
