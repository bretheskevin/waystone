#pragma once
#include "list_screen.h"
#include "session.h"
#include "sync_worker.h"
#include "saves.h"
#include "icon_tex.h"
#include <vector>

class TitleListScreen : public ListScreen {
public:
    TitleListScreen(Session* session);
    TitleListScreen(Session* session, const std::vector<TitleInfo>& preloaded_titles);
    ~TitleListScreen();
    void poll();

protected:
    // ListScreen hooks
    const char* screen_title()          { return "Your Saves"; }
    float       status_area_height() const;
    void        draw_top_status(C2D_TextBuf buf, float status_y);
    size_t      item_count()            { return titles_.size(); }
    float       row_height() const      { return 46.0f; }
    bool        fill_height() const     { return true; }
    void        draw_row(C2D_TextBuf buf, size_t i,
                         float x, float y, float w, float h, bool focused);
    std::vector<Action> actions();
    void        on_action(int id);
    bool        has_back() const        { return false; }

private:
    enum ActionId { ACT_SYNC = 0, ACT_CONFLICTS, ACT_SETTINGS, ACT_SNAPSHOTS, ACT_HISTORY };

    Session* session_;
    std::vector<TitleInfo> titles_;
    SyncWorker* worker_;
    std::string status_text_;
    bool syncing_;
    std::vector<IconImage*> icon_cache_;

    void init_icon_cache();
    void start_sync();
    void start_sync_or_gate();

    TitleListScreen(const TitleListScreen&);
    TitleListScreen& operator=(const TitleListScreen&);
};
