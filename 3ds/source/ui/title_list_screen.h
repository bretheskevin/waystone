#pragma once
#include "list_screen.h"
#include "session.h"
#include "saves.h"
#include "icon_tex.h"
#include <vector>

class TitleListScreen : public ListScreen {
public:
    TitleListScreen(Session* session);
    TitleListScreen(Session* session, const std::vector<TitleInfo>& preloaded_titles);
    ~TitleListScreen();
    void handle_input(u32 kDown, touchPosition touch);

protected:
    // ListScreen hooks
    const char* screen_title()          { return "Your Saves"; }
    const char* corner_label();
    size_t      item_count()            { return titles_.size(); }
    float       row_height() const      { return 46.0f; }
    bool        fill_height() const     { return true; }
    void        draw_row(C2D_TextBuf buf, size_t i,
                         float x, float y, float w, float h, bool focused);
    void        draw_detail(C2D_TextBuf buf, float area_y, float area_h);
    std::vector<Action> actions();
    void        on_action(int id);
    bool        has_back() const        { return false; }

private:
    enum ActionId { ACT_SYNC = 0, ACT_SYNC_ALL, ACT_CONFLICTS, ACT_SETTINGS, ACT_SNAPSHOTS, ACT_HISTORY };

    Session* session_;
    std::vector<TitleInfo> titles_;
    std::vector<IconImage*> icon_cache_;
    Rect sync_all_btn_rect_;  // touch target for the bottom-screen Sync All button

    void init_icon_cache();
    void start_sync(std::vector<TitleInfo> titles);
    void start_sync_or_gate(std::vector<TitleInfo> titles);

    TitleListScreen(const TitleListScreen&);
    TitleListScreen& operator=(const TitleListScreen&);
};
