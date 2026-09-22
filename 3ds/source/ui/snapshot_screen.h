#pragma once
#include "restore_browse_screen.h"
#include "session.h"
#include "snapshot_worker.h"
#include <vector>
#include <string>

class SnapshotScreen : public RestoreBrowseScreen {
public:
    SnapshotScreen(Session* session, TitleInfo selected);
    ~SnapshotScreen();
    void poll();

protected:
    // ListScreen content hooks
    const char* screen_title()          { return "Snapshots"; }
    std::string subtitle();
    size_t      item_count()            { return items_.size(); }
    float       row_height() const      { return 28.0f; }
    void        draw_row(C2D_TextBuf buf, size_t i,
                         float x, float y, float w, float h, bool focused);
    void        draw_selected_detail(C2D_TextBuf buf, float area_y, float area_h);

    // RestoreBrowseScreen accessors
    BrowsePhase browse_phase()              const { return phase_; }
    void        browse_start_restore(size_t index);
    const char* confirm_line1()             const { return "Restore this snapshot?"; }
    const char* log_tag()                   const { return "snapshot"; }

private:
    Session* session_;
    TitleInfo title_;
    SnapshotWorker* worker_;
    std::vector<SnapshotEntry> items_;
    BrowsePhase phase_;

    SnapshotScreen(const SnapshotScreen&);
    SnapshotScreen& operator=(const SnapshotScreen&);
};
