#pragma once
#include <citro2d.h>
#include <string>

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

enum class ButtonStyle { PRIMARY, SECONDARY };

void draw_text(C2D_TextBuf buf, float x, float y, float z, float scale, u32 color, const char* str);
void draw_text_centered(C2D_TextBuf buf, float cx, float y, float z, float scale, u32 color, const char* str, float area_w);
float text_width(C2D_TextBuf buf, float scale, const char* str);
float text_height(C2D_TextBuf buf, float scale, const char* str);
void draw_rounded_rect(float x, float y, float z, float w, float h, float radius, u32 color);
Rect draw_button(C2D_TextBuf buf, float x, float y, float w, float h, const char* label, ButtonStyle style, bool focused);
Rect draw_text_field_row(C2D_TextBuf buf, float x, float y, float w, const char* label, const char* value, bool is_secret, bool focused, const char* hint);
void draw_status_bar(C2D_TextBuf buf, C3D_RenderTarget* top, const char* text, u32 color);
void draw_progress_bar(float x, float y, float w, float h, float progress, u32 fill_color, u32 bg_color);
void draw_footer_hint(C2D_TextBuf buf, const char* text);
void draw_step_dots(C2D_TextBuf buf, float cx, float y, size_t current, size_t total);
