#include "widgets.h"
#include "theme.h"
#include <cstring>

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
void draw_footer_hint(C2D_TextBuf buf, const char* text) {
    float th = text_height(buf, TEXT_SM, text);
    float y = (float)SCREEN_BOT_H - SP_MD - th;
    draw_text_centered(buf, 0, y, Z, TEXT_SM, CLR_TEXT_HINT, text, (float)SCREEN_BOT_W);
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
