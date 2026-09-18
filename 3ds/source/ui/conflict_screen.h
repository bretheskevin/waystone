#pragma once
#include "list_screen.h"
#include "session.h"
#include "conflict_worker.h"
#include <vector>
#include <string>

class ConflictScreen : public ListScreen {
public:
    ConflictScreen(Session* session, std::vector<TitleInfo> titles);
    ~ConflictScreen();
    void poll();

protected:
    // ListScreen hooks
    const char* screen_title()          { return "Conflict Inbox"; }
    std::string subtitle();
    float       status_area_height() const;
    void        draw_top_status(C2D_TextBuf buf, float status_y);
    size_t      item_count()            { return items_.size(); }
    float       row_height() const      { return 28.0f; }
    void        draw_row(C2D_TextBuf buf, size_t i,
                         float x, float y, float w, bool focused);
    void        draw_detail(C2D_TextBuf buf, float area_y, float area_h);
    std::vector<Action> actions();
    void        on_action(int id);
    bool        has_back() const        { return true; }
    void        on_back();              // deferred-cancel or confirm-cancel
    bool        modal_active() const    { return confirm_remote_; }

private:
    enum ActionId { ACT_KEEP_LOCAL = 0, ACT_KEEP_REMOTE, ACT_CONFIRM };

    Session* session_;
    ConflictWorker* worker_;
    std::vector<ConflictItem> items_;
    bool confirm_remote_;
    std::string status_text_;
    ConflictPhase phase_;

    ConflictScreen(const ConflictScreen&);
    ConflictScreen& operator=(const ConflictScreen&);
};
