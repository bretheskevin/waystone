#pragma once
#include "list_screen.h"
#include "browse_phase.h"
#include <string>
#include <vector>

// Base for screens that scan a browse-able list and offer a restore-confirm flow.
// Snapshot and History screens share this flow; only their worker/item types differ.
class RestoreBrowseScreen : public ListScreen {
public:
    RestoreBrowseScreen();
    virtual ~RestoreBrowseScreen();

protected:
    // --- Pure-virtual: subclass provides phase, restore, and banner text ---
    virtual BrowsePhase browse_phase()              const = 0;
    virtual void        browse_start_restore(size_t index) = 0;
    virtual const char* confirm_line1()             const = 0;
    virtual const char* log_tag()                   const = 0;

    // Non-confirm detail (items view); called from draw_detail when not confirming.
    virtual void draw_selected_detail(C2D_TextBuf buf,
                                      float area_y, float area_h) = 0;

    // --- ListScreen hooks implemented in base ---
    float               status_area_height() const;
    void                draw_top_status(C2D_TextBuf buf, float status_y);
    void                draw_detail(C2D_TextBuf buf, float area_y, float area_h);
    std::vector<Action> actions();
    void                on_action(int id);
    void                on_back();
    bool                modal_active() const;

    // --- Shared state owned by base; subclass poll() writes status_text_ ---
    bool        confirm_restore_;
    std::string status_text_;

private:
    enum ActionId { ACT_RESTORE = 0, ACT_CONFIRM };

    RestoreBrowseScreen(const RestoreBrowseScreen&);
    RestoreBrowseScreen& operator=(const RestoreBrowseScreen&);
};
