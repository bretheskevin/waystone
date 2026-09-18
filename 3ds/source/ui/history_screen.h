#pragma once
#include "restore_browse_screen.h"
#include "session.h"
#include "history_worker.h"
#include <vector>
#include <string>

class HistoryScreen : public RestoreBrowseScreen {
public:
    HistoryScreen(Session* session, TitleInfo selected);
    ~HistoryScreen();
    void poll();

protected:
    // ListScreen content hooks
    const char* screen_title()          { return "History"; }
    std::string subtitle();
    size_t      item_count()            { return items_.size(); }
    float       row_height() const      { return 28.0f; }
    void        draw_row(C2D_TextBuf buf, size_t i,
                         float x, float y, float w, bool focused);
    void        draw_selected_detail(C2D_TextBuf buf, float area_y, float area_h);

    // RestoreBrowseScreen accessors
    BrowsePhase browse_phase()              const { return phase_; }
    void        browse_start_restore(size_t index);
    const char* confirm_line1()             const { return "Restore this version?"; }
    const char* log_tag()                   const { return "history"; }

private:
    Session* session_;
    TitleInfo title_;
    HistoryWorker* worker_;
    std::vector<HistoryEntry> items_;
    BrowsePhase phase_;

    HistoryScreen(const HistoryScreen&);
    HistoryScreen& operator=(const HistoryScreen&);
};
