#include "widgets.h"
#include "theme.h"
#include <cstring>
#include <cmath>
#include <string>

static const float Z = 0.5f;

void draw_text(C2D_TextBuf buf, float x, float y, float z, float scale, u32 color, const char* str) {
    if (!str || str[0]=='\0') return;
    C2D_Text text; C2D_TextParse(&text, buf, str); C2D_TextOptimize(&text);
    C2D_DrawText(&text, C2D_WithColor, x, y, z, scale, scale, color);
}
static void text_dimensions(C2D_TextBuf buf, float scale, const char* str, float* w, float* h) {
    if (!str || str[0]=='\0') { *w = 0.0f; *h = 0.0f; return; }
    C2D_Text text; C2D_TextParse(&text, buf, str); C2D_TextOptimize(&text);
    C2D_TextGetDimensions(&text, scale, scale, w, h);
}
float text_width(C2D_TextBuf buf, float scale, const char* str) {
    float w=0.0f,h=0.0f; text_dimensions(buf, scale, str, &w, &h); return w;
}
float text_height(C2D_TextBuf buf, float scale, const char* str) {
    float w=0.0f,h=0.0f; text_dimensions(buf, scale, str, &w, &h); return h;
}
void draw_text_centered(C2D_TextBuf buf, float cx, float y, float z, float scale, u32 color, const char* str, float area_w) {
    float tw = text_width(buf, scale, str); draw_text(buf, cx + (area_w - tw)/2.0f, y, z, scale, color, str);
}
void draw_text_centered_fit(C2D_TextBuf buf, float cx, float y, float z, float base_scale, u32 color, const char* str, float area_w, float min_scale) {
    if (!str || str[0]=='\0') return;
    float tw = text_width(buf, base_scale, str);
    float scale = base_scale;
    if (tw > area_w && tw > 0.0f) {
        scale = base_scale * (area_w / tw);
        if (scale < min_scale) scale = min_scale;
    }
    draw_text_centered(buf, cx, y, z, scale, color, str, area_w);
}
float draw_text_wrapped_centered(C2D_TextBuf buf, float cx, float y, float z, float scale, u32 color, const char* str, float area_w, float line_h) {
    if (!str || str[0]=='\0') return y;
    std::string s(str);
    size_t i = 0, n = s.size();
    while (i < n) {
        size_t line_end = i;
        while (line_end < n) {
            // advance one UTF-8 code point past line_end
            size_t cp = line_end + 1;
            while (cp < n && (static_cast<unsigned char>(s[cp]) & 0xC0) == 0x80) cp++;
            std::string cand = s.substr(i, cp - i);
            if (text_width(buf, scale, cand.c_str()) > area_w && line_end > i) break;
            line_end = cp;
        }
        std::string line = s.substr(i, line_end - i);
        draw_text_centered(buf, cx, y, z, scale, color, line.c_str(), area_w);
        y += line_h;
        i = line_end;
    }
    return y;
}
std::string truncate_text_fit(C2D_TextBuf buf, float scale, const char* str, float max_w) {
    static const char* ELLIPSIS = "\xe2\x80\xa6";
    if (!str || str[0] == '\0') return std::string();
    if (text_width(buf, scale, str) <= max_w) return std::string(str);
    float ew     = text_width(buf, scale, ELLIPSIS);
    float budget = max_w - ew;
    std::string s(str);
    if (budget > 0.0f) {
        while (!s.empty() && text_width(buf, scale, s.c_str()) > budget) {
            while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80)
                s.resize(s.size() - 1);
            if (!s.empty())
                s.resize(s.size() - 1);
        }
    } else {
        s.clear();
    }
    return s + ELLIPSIS;
}
void draw_spinner(C2D_TextBuf buf, float cx, float cy, float angle) {
    (void)buf;
    const int   N      = 4;
    const float dot_r  = 4.0f;
    const float spread = 18.0f;
    float base_x = cx - ((float)(N - 1) * spread) / 2.0f;
    for (int i = 0; i < N; i++) {
        float alpha = 0.5f + 0.5f * sinf(angle + (float)i * 1.57f);
        u8    a     = static_cast<u8>(255.0f * alpha);
        u32   color = C2D_Color32(0x63, 0x66, 0xF1, a);
        float x     = base_x + (float)i * spread;
        draw_rounded_rect(x - dot_r, cy - dot_r, 0.5f, dot_r * 2.0f, dot_r * 2.0f, dot_r, color);
    }
}
void draw_rounded_rect(float x, float y, float z, float w, float h, float radius, u32 color) {
    if (radius <= 0.0f) { C2D_DrawRectSolid(x,y,z,w,h,color); return; }
    float r = radius;
    C2D_DrawRectSolid(x+r, y, z, w-2*r, h, color);
    C2D_DrawRectSolid(x, y+r, z, r, h-2*r, color);
    C2D_DrawRectSolid(x+w-r, y+r, z, r, h-2*r, color);
    C2D_DrawCircleSolid(x+r,     y+r,     z, r, color);
    C2D_DrawCircleSolid(x+w-r,   y+r,     z, r, color);
    C2D_DrawCircleSolid(x+r,     y+h-r,   z, r, color);
    C2D_DrawCircleSolid(x+w-r,   y+h-r,   z, r, color);
}
Rect draw_button(C2D_TextBuf buf, float x, float y, float w, float h, const char* label, ButtonStyle style, bool focused) {
    u32 bg = (style==ButtonStyle::PRIMARY)?CLR_BTN_PRIMARY:CLR_CARD_BG;
    u32 fg = (style==ButtonStyle::PRIMARY)?CLR_BTN_TEXT:CLR_TEXT;
    if (focused) draw_rounded_rect(x-2,y-2,Z-0.01f,w+4,h+4,RAD_MD+2,CLR_ACCENT);
    draw_rounded_rect(x,y,Z,w,h,RAD_MD,bg);
    draw_text_centered(buf, x, y+(h-text_height(buf,TEXT_BASE,label))/2.0f, Z+0.01f, TEXT_BASE, fg, label, w);
    Rect rect = {x,y,w,h}; return rect;
}
Rect draw_text_field_row(C2D_TextBuf buf, float x, float y, float w, const char* label, const char* value, bool is_secret, bool focused, const char* hint) {
    float row_h = SP_2XL + SP_MD;
    if (focused) draw_rounded_rect(x-2,y-2,Z-0.01f,w+4,row_h+4,RAD_MD+2,CLR_ACCENT);
    draw_rounded_rect(x,y,Z,w,row_h,RAD_MD,CLR_FIELD_BG);
    draw_text(buf, x+SP_MD, y+SP_SM, Z+0.01f, TEXT_SM, CLR_TEXT_HINT, label);
    const char* display_val = value; std::string masked;
    if (is_secret && value && value[0]!='\0') { masked = std::string(strlen(value),'*'); display_val = masked.c_str(); }
    if (!display_val || display_val[0]=='\0') {
        display_val = hint ? hint : "not set";
        draw_text(buf, x+SP_MD, y+SP_SM+SP_LG, Z+0.01f, TEXT_BASE, CLR_NEUTRAL_400, display_val);
    } else {
        draw_text(buf, x+SP_MD, y+SP_SM+SP_LG, Z+0.01f, TEXT_BASE, CLR_TEXT, display_val);
    }
    Rect rect = {x,y,w,row_h}; return rect;
}
void draw_status_bar(C2D_TextBuf buf, C3D_RenderTarget* /*top*/, const char* text, u32 color) {
    float y = SCREEN_TOP_H - SP_2XL;
    draw_text_centered(buf, 0, y, Z, TEXT_BASE, color, text, (float)SCREEN_TOP_W);
}
void draw_progress_bar(float x, float y, float w, float h, float progress, u32 fill_color, u32 bg_color) {
    draw_rounded_rect(x,y,Z,w,h,h/2.0f,bg_color);
    if (progress > 0.0f) { float fw = w*(progress>1.0f?1.0f:progress); if (fw>h) draw_rounded_rect(x,y,Z+0.01f,fw,h,h/2.0f,fill_color); }
}
void draw_confirm_banner(C2D_TextBuf buf, float area_y, float area_h,
                         const char* line1, const char* line2, const char* line3,
                         float banner_h) {
    float banner_y = area_y + (area_h - banner_h) / 2.0f;
    if (banner_y < area_y) banner_y = area_y;
    C2D_DrawRectSolid(0, banner_y, 0.6f, (float)SCREEN_BOT_W, banner_h, CLR_WARNING);
    draw_text_centered(buf, 0, banner_y + 4.0f, 0.61f, TEXT_BASE,
                       CLR_NEUTRAL_900, line1, (float)SCREEN_BOT_W);
    if (line3 && line3[0]) {
        draw_text_centered(buf, 0, banner_y + 20.0f, 0.61f, TEXT_SM,
                           CLR_NEUTRAL_800, line2, (float)SCREEN_BOT_W);
        draw_text_centered(buf, 0, banner_y + 36.0f, 0.61f, TEXT_SM,
                           CLR_NEUTRAL_800, line3, (float)SCREEN_BOT_W);
    } else {
        draw_text_centered(buf, 0, banner_y + 22.0f, 0.61f, TEXT_SM,
                           CLR_NEUTRAL_800, line2, (float)SCREEN_BOT_W);
    }
}
void draw_footer_hint(C2D_TextBuf buf, const char* text) {
    float th = text_height(buf, TEXT_SM, text);
    float y = (float)SCREEN_BOT_H - SP_MD - th;
    // Shrink to fit so long shortcut lines never overflow the 320px bottom screen.
    draw_text_centered_fit(buf, 0, y, Z, TEXT_SM, CLR_TEXT_HINT, text,
                           (float)SCREEN_BOT_W - 2.0f * (float)SP_XS, TEXT_SM * 0.6f);
}
void draw_step_dots(C2D_TextBuf buf, float cx, float y, size_t current, size_t total) {
    (void)buf;
    float dot_r = 3.0f, gap = SP_MD;
    float total_w = (float)total*dot_r*2.0f + (float)(total-1)*gap;
    float start_x = cx - total_w/2.0f + dot_r;
    for (size_t i=0;i<total;i++){ u32 c=(i==current)?CLR_PRIMARY_500:CLR_NEUTRAL_200; float dx=start_x+(float)i*(dot_r*2.0f+gap); C2D_DrawCircleSolid(dx,y,Z,dot_r,c); }
}

void draw_image(C2D_Image img, float x, float y, float w, float h) {
    if (!img.tex || !img.subtex) return;
    float sx = w / (float)img.subtex->width;
    float sy = h / (float)img.subtex->height;
    C2D_DrawImageAt(img, x, y, Z, NULL, sx, sy);
}
