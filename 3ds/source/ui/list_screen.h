#pragma once
#include "screen.h"
#include "widgets.h"
#include "theme.h"
#include "keymap_3ds.h"
#include <vector>
#include <string>

struct Action {
    u32         key;      // from ws_key(); 0 = touch-only
    const char* glyph;   // from ws_glyph(); drawn before the label
    const char* label;   // "Sync All" -- always a string literal (no heap alloc per frame)
    int         id;      // passed to on_action()
    bool        enabled;
    ButtonStyle style;   // PRIMARY / SECONDARY
};

inline Action make_action(WsAction wa, int id, bool enabled, ButtonStyle style) {
    Action a = { ws_key(wa), ws_glyph(ws_button(wa)), ws_label(wa), id, enabled, style };
    return a;
}

class App;  // forward; include app.h only in the .cpp

class ListScreen : public Screen {
public:
    ListScreen();
    virtual ~ListScreen();

    // --- Screen overrides (final: subclasses use hooks below) ---
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    // poll() is NOT overridden here -- stays virtual no-op from Screen.
    // Subclasses that have workers override Screen::poll() directly.

protected:
    // ===== Content hooks (subclass implements) =====

    // Title bar
    virtual const char* screen_title() = 0;             // "Your Saves"
    virtual std::string subtitle() { return ""; }        // "30 games"

    // Optional top-screen status area between the header and the list.
    // Draw status text, progress bars, etc. starting at (0, status_y).
    // status_y is provided by the base; draw within status_area_height() px.
    virtual float status_area_height() const { return 0.0f; }
    virtual void  draw_top_status(C2D_TextBuf buf, float status_y) { (void)buf; (void)status_y; }

    // List content
    virtual size_t item_count() = 0;
    virtual float  row_height() const { return 46.0f; }
    // When true, visible rows grow to consume 100% of the remaining top-screen
    // height (used by the title list). Default keeps fixed-height rows.
    virtual bool   fill_height() const { return false; }
    // Draw one row's CONTENT inside the already-drawn card background.
    // (x, y) is the card's top-left; w/h are the card size; focused is cursor==i.
    virtual void   draw_row(C2D_TextBuf buf, size_t i,
                            float x, float y, float w, float h, bool focused) = 0;

    // Bottom-screen detail area (above the action bar). Optional.
    virtual void draw_detail(C2D_TextBuf buf, float area_y, float area_h) { (void)buf; (void)area_y; (void)area_h; }

    // Action bar
    virtual std::vector<Action> actions() = 0;   // rebuilt each frame from current state
    virtual void on_action(int id) = 0;

    // Navigation
    virtual bool has_back() const { return true; }
    virtual void on_back();   // default: App::instance().pop_screen()

    // Return true to block DPad navigation (e.g. during a confirm modal).
    // The base still dispatches action keys and touch, and calls on_back() for B.
    virtual bool modal_active() const { return false; }

    // ===== Shared state =====
    size_t cursor_;
    size_t scroll_offset_;

    // Computed from (top screen height - header - status - margins) / row pitch.
    size_t visible_rows();

private:
    void clamp_scroll();
    // Shared top-screen list geometry: collapses the header when there is no
    // subtitle, drops the idle status band, and (when fill_height()) grows the
    // rows so the list consumes 100% of the remaining height.
    void list_metrics(float& list_top, size_t& vis, float& pitch, float& card_h);
    void draw_action_bar(C2D_TextBuf buf, float bar_y, const std::vector<Action>& acts);

    // Stored each frame during draw_bottom so touch hit-test in handle_input
    // uses the exact same rects (single source of truth).
    std::vector<Rect>   action_rects_;
    std::vector<int>    action_ids_;     // parallel to action_rects_: the id of each drawn action

    ListScreen(const ListScreen&);
    ListScreen& operator=(const ListScreen&);
};
