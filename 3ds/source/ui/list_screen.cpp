#include "list_screen.h"
#include "app.h"
#include <cstdio>
#include <cstring>

// ---- Top-screen layout constants (400x240) ----
static const float BRAND_Y            = 8.0f;
static const float TITLE_Y            = 26.0f;
static const float SUBTITLE_Y         = 50.0f;
static const float HEADER_BASE_Y      = 68.0f;   // status / list starts after this
static const float HEADER_BASE_Y_NOSUB = 50.0f;  // no subtitle: list starts right after the title
static const float LIST_BOTTOM_MARGIN = 16.0f;
static const float SB_W               = 4.0f;    // scrollbar track width
static const float SB_GAP             = 6.0f;    // gap between list and scrollbar
static const float COUNTER_H          = 14.0f;   // "n / total" text height

// ---- Bottom-screen layout constants (320x240) ----
static const float ABTN_H           = 28.0f;   // action button height
static const float ABTN_GAP_H       = 8.0f;    // horizontal gap between buttons
static const float ABTN_GAP_V       = 6.0f;    // vertical gap between button rows
static const float ABTN_MAX_PER_ROW = 3.0f;
static const float FOOTER_H         = 18.0f;   // "B: Back" footer area
static const float DETAIL_TOP       = (float)SP_MD;

ListScreen::ListScreen() : cursor_(0), scroll_offset_(0) {}
ListScreen::~ListScreen() {}

void ListScreen::on_back() {
    App::instance().pop_screen();
}

// ---- visible_rows ----

size_t ListScreen::visible_rows() {
    float list_top, pitch, card_h;
    size_t vis;
    list_metrics(list_top, vis, pitch, card_h);
    return vis;
}

void ListScreen::list_metrics(float& list_top, size_t& vis,
                              float& pitch, float& card_h) {
    // Collapse the header band when this screen has no subtitle.
    float header = subtitle().empty() ? HEADER_BASE_Y_NOSUB : HEADER_BASE_Y;
    list_top = header + status_area_height();
    float list_bottom = (float)SCREEN_TOP_H - LIST_BOTTOM_MARGIN;
    float avail = list_bottom - list_top;
    float rh = row_height();
    float min_pitch = rh + (float)SP_XS;
    if (avail < rh) { vis = 1; card_h = rh; pitch = min_pitch; return; }
    size_t n = (size_t)((avail + (float)SP_XS) / min_pitch);
    if (n < 1) n = 1;
    vis = n;
    if (fill_height()) {
        // Grow rows so the list consumes 100% of the available height,
        // keeping a fixed SP_XS gap between cards.
        card_h = (avail - (float)(n - 1) * (float)SP_XS) / (float)n;
        pitch  = card_h + (float)SP_XS;
    } else {
        card_h = rh;
        pitch  = min_pitch;
    }
}

// ---- scroll clamping ----

void ListScreen::clamp_scroll() {
    size_t count = item_count();
    if (count == 0) {
        cursor_        = 0;
        scroll_offset_ = 0;
        return;
    }
    if (cursor_ >= count) cursor_ = count - 1;
    size_t vis = visible_rows();
    if (cursor_ < scroll_offset_)
        scroll_offset_ = cursor_;
    else if (cursor_ >= scroll_offset_ + vis)
        scroll_offset_ = cursor_ - vis + 1;
    if (count <= vis) {
        scroll_offset_ = 0;
    } else if (scroll_offset_ + vis > count) {
        scroll_offset_ = count - vis;
    }
}

// ---- draw_top ----

void ListScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    size_t count = item_count();

    // Brand
    draw_text_centered(buf, 0, BRAND_Y, 0.5f, TEXT_SM, CLR_NEUTRAL_400,
                       "Waystone", (float)SCREEN_TOP_W);
    // Title
    draw_text_centered(buf, 0, TITLE_Y, 0.5f, TEXT_XL, CLR_TEXT,
                       screen_title(), (float)SCREEN_TOP_W);
    // Subtitle
    std::string sub = subtitle();
    if (!sub.empty()) {
        draw_text_centered(buf, 0, SUBTITLE_Y, 0.5f, TEXT_BASE, CLR_NEUTRAL_400,
                           sub.c_str(), (float)SCREEN_TOP_W);
    }
    // ---- List geometry (header collapse + optional fill-height) ----
    float list_top, pitch, card_h;
    size_t vis;
    list_metrics(list_top, vis, pitch, card_h);

    // Status area (between header and list); nothing drawn when height is 0.
    float status_h = status_area_height();
    if (status_h > 0.0f) {
        draw_top_status(buf, list_top - status_h);
    }

    float list_x      = (float)SP_XL;                        // 16px left margin
    float list_w_full = (float)SCREEN_TOP_W - 2.0f * list_x;

    bool has_scroll   = count > vis;
    float list_w      = list_w_full - (has_scroll ? (SB_W + SB_GAP) : 0.0f);

    size_t end = scroll_offset_ + vis;
    if (end > count) end = count;

    float y = list_top;
    for (size_t i = scroll_offset_; i < end; i++) {
        bool focused = (cursor_ == i);
        u32  bg      = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        if (focused) {
            draw_rounded_rect(list_x - 1.0f, y - 1.0f, 0.49f,
                              list_w + 2.0f, card_h + 2.0f,
                              RAD_SM + 1.0f, CLR_ACCENT);
        }
        draw_rounded_rect(list_x, y, 0.5f, list_w, card_h, RAD_SM, bg);
        draw_row(buf, i, list_x, y, list_w, card_h, focused);
        y += pitch;
    }

    // Scrollbar (track + thumb) on the right edge of the list area
    if (has_scroll) {
        float sb_x   = list_x + list_w + SB_GAP;
        float list_h = (float)vis * pitch - (float)SP_XS;
        // Track
        draw_rounded_rect(sb_x, list_top, 0.5f, SB_W, list_h,
                          SB_W / 2.0f, CLR_NEUTRAL_200);
        // Thumb
        float total_f = (float)count;
        float thumb_h = list_h * (float)vis / total_f;
        if (thumb_h < 10.0f) thumb_h = 10.0f;
        float max_off = total_f - (float)vis;
        float t       = (max_off > 0.0f) ? (float)scroll_offset_ / max_off : 0.0f;
        float thumb_y = list_top + (list_h - thumb_h) * t;
        draw_rounded_rect(sb_x, thumb_y, 0.51f, SB_W, thumb_h,
                          SB_W / 2.0f, CLR_ACCENT);
    }

    // Counter: "n / total" below the list
    if (has_scroll) {
        char counter[32];
        snprintf(counter, sizeof(counter), "%zu / %zu", end, count);
        float counter_y = list_top + (float)vis * pitch;
        draw_text_centered(buf, 0, counter_y, 0.5f, TEXT_SM, CLR_TEXT_HINT,
                           counter, (float)SCREEN_TOP_W);
    }

    // Corner label: right-aligned with the list edge, on the counter's row.
    const char* corner = corner_label();
    if (corner && corner[0] != '\0') {
        float corner_w = text_width(buf, TEXT_SM, corner);
        draw_text(buf, list_x + list_w_full - corner_w, list_top + (float)vis * pitch,
                  0.5f, TEXT_SM, CLR_TEXT_HINT, corner);
    }
}

// ---- draw_bottom ----

void ListScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();

    // Compute action bar geometry from bottom up
    std::vector<Action> acts = actions();
    size_t n    = acts.size();
    size_t rows = (n > 0)
        ? ((n + (size_t)ABTN_MAX_PER_ROW - 1) / (size_t)ABTN_MAX_PER_ROW)
        : 0;

    float footer_y = (float)SCREEN_BOT_H;
    if (has_back() && !modal_active()) {
        footer_y -= FOOTER_H;
        draw_footer_hint(buf, ws_hint(WsAction::Back).c_str());
    }

    float bar_total_h = (rows > 0)
        ? (float)rows * ABTN_H + (float)(rows - 1) * ABTN_GAP_V
        : 0.0f;
    float bar_y = footer_y - (float)SP_MD - bar_total_h;

    // Detail area: from DETAIL_TOP to bar_y - margin
    float detail_h = bar_y - (float)SP_SM - DETAIL_TOP;
    if (detail_h > 0.0f) {
        draw_detail(buf, DETAIL_TOP, detail_h);
    }

    // Action bar
    draw_action_bar(buf, bar_y, acts);
}

// ---- draw_action_bar ----

void ListScreen::draw_action_bar(C2D_TextBuf buf, float bar_y,
                                  const std::vector<Action>& acts) {
    action_rects_.clear();
    action_ids_.clear();
    size_t n = acts.size();
    if (n == 0) return;

    float avail_w = (float)SCREEN_BOT_W - 2.0f * (float)SP_MD;
    float x_start = (float)SP_MD;
    float y       = bar_y;
    size_t idx    = 0;

    while (idx < n) {
        size_t remaining = n - idx;
        size_t per_row   = remaining;
        if (per_row > (size_t)ABTN_MAX_PER_ROW) per_row = (size_t)ABTN_MAX_PER_ROW;

        float btn_w = (avail_w - (float)(per_row - 1) * ABTN_GAP_H) / (float)per_row;
        float bx    = x_start;

        for (size_t j = 0; j < per_row; j++) {
            const Action& act = acts[idx];
            char btn_text[64];
            if (act.glyph && act.glyph[0] != '\0') {
                snprintf(btn_text, sizeof(btn_text), "%s %s", act.glyph, act.label);
            } else {
                snprintf(btn_text, sizeof(btn_text), "%s", act.label);
            }

            Rect r;
            if (act.enabled) {
                r = draw_button(buf, bx, y, btn_w, ABTN_H,
                                btn_text, act.style, false);
            } else {
                // Greyed-out disabled button
                draw_rounded_rect(bx, y, 0.5f, btn_w, ABTN_H,
                                  RAD_MD, CLR_NEUTRAL_200);
                float th = text_height(buf, TEXT_BASE, btn_text);
                draw_text_centered_fit(buf, bx + (float)SP_XS,
                                       y + (ABTN_H - th) / 2.0f,
                                       0.51f, TEXT_BASE, CLR_NEUTRAL_400,
                                       btn_text, btn_w - 2.0f * (float)SP_XS, TEXT_BASE * 0.6f);
                r.x = bx; r.y = y; r.w = btn_w; r.h = ABTN_H;
            }
            action_rects_.push_back(r);
            action_ids_.push_back(act.id);

            bx  += btn_w + ABTN_GAP_H;
            idx++;
        }

        y += ABTN_H + ABTN_GAP_V;
    }
}

// ---- handle_input ----

void ListScreen::handle_input(u32 kDown, touchPosition touch) {
    size_t count = item_count();

    // DPad navigation (blocked during modal)
    if (!modal_active() && count > 0) {
        if (kDown & KEY_DUP) {
            cursor_ = (cursor_ == 0) ? count - 1 : cursor_ - 1;
            printf("[ui] cursor up -> %zu\n", cursor_);
        }
        if (kDown & KEY_DDOWN) {
            cursor_ = (cursor_ + 1) % count;
            printf("[ui] cursor down -> %zu\n", cursor_);
        }
        clamp_scroll();
    }

    // B: Back (or Cancel while a confirm modal is up)
    u32 back_key = modal_active() ? ws_key(WsAction::Cancel) : ws_key(WsAction::Back);
    if (kDown & back_key) {
        if (has_back() || modal_active()) {
            printf("[ui] back pressed\n");
            on_back();
            return;
        }
    }

    // Action key dispatch
    std::vector<Action> acts = actions();
    for (size_t i = 0; i < acts.size(); i++) {
        if (acts[i].key != 0 && acts[i].enabled && (kDown & acts[i].key)) {
            printf("[ui] action key 0x%lx -> id=%d\n",
                   (unsigned long)acts[i].key, acts[i].id);
            on_action(acts[i].id);
            return;
        }
    }

    // Touch dispatch (bottom screen only)
    if (touch.px != 0 || touch.py != 0) {
        for (size_t i = 0; i < action_rects_.size(); i++) {
            if (action_rects_[i].contains((float)touch.px, (float)touch.py)) {
                if (i < action_ids_.size()) {
                    int aid = action_ids_[i];
                    for (size_t j = 0; j < acts.size(); j++) {
                        if (acts[j].id == aid && acts[j].enabled) {
                            printf("[ui] touch action id=%d\n", aid);
                            on_action(aid);
                            return;
                        }
                    }
                }
            }
        }
    }
}
