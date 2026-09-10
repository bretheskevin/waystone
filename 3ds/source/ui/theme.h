#pragma once
#include <citro2d.h>
// Primary (indigo)
#define CLR_PRIMARY_50   C2D_Color32(0xEE,0xF2,0xFF,0xFF)
#define CLR_PRIMARY_100  C2D_Color32(0xE0,0xE7,0xFF,0xFF)
#define CLR_PRIMARY_200  C2D_Color32(0xC7,0xD2,0xFE,0xFF)
#define CLR_PRIMARY_500  C2D_Color32(0x63,0x66,0xF1,0xFF)
#define CLR_PRIMARY_600  C2D_Color32(0x4F,0x46,0xE5,0xFF)
#define CLR_PRIMARY_700  C2D_Color32(0x43,0x38,0xCA,0xFF)
// Neutral
#define CLR_NEUTRAL_50   C2D_Color32(0xFA,0xFA,0xFA,0xFF)
#define CLR_NEUTRAL_100  C2D_Color32(0xF4,0xF4,0xF5,0xFF)
#define CLR_NEUTRAL_200  C2D_Color32(0xE4,0xE4,0xE7,0xFF)
#define CLR_NEUTRAL_400  C2D_Color32(0xA1,0xA1,0xAA,0xFF)
#define CLR_NEUTRAL_500  C2D_Color32(0x71,0x71,0x7A,0xFF)
#define CLR_NEUTRAL_800  C2D_Color32(0x27,0x27,0x2A,0xFF)
#define CLR_NEUTRAL_900  C2D_Color32(0x18,0x18,0x1B,0xFF)
// Status
#define CLR_SUCCESS  C2D_Color32(0x22,0xC5,0x5E,0xFF)
#define CLR_WARNING  C2D_Color32(0xF5,0x9E,0x0B,0xFF)
#define CLR_ERROR    C2D_Color32(0xEF,0x44,0x44,0xFF)
#define CLR_SYNC     C2D_Color32(0x06,0xB6,0xD4,0xFF)
#define CLR_WHITE    C2D_Color32(0xFF,0xFF,0xFF,0xFF)
// Semantic aliases
#define CLR_BG_TOP      CLR_NEUTRAL_900
#define CLR_BG_BOTTOM   CLR_NEUTRAL_50
#define CLR_TEXT        CLR_NEUTRAL_800
#define CLR_TEXT_HINT   CLR_NEUTRAL_500
#define CLR_CARD_BG     CLR_WHITE
#define CLR_CARD_BORDER CLR_NEUTRAL_200
#define CLR_BTN_PRIMARY CLR_PRIMARY_600
#define CLR_BTN_TEXT    CLR_WHITE
#define CLR_FIELD_BG    CLR_NEUTRAL_100
#define CLR_ACCENT      CLR_PRIMARY_500
// Spacing (px, 240p)
#define SP_XS 2
#define SP_SM 4
#define SP_MD 8
#define SP_LG 12
#define SP_XL 16
#define SP_2XL 24
// Corner radii
#define RAD_SM 2.0f
#define RAD_MD 4.0f
#define RAD_LG 6.0f
// Text scale (citro2d system font)
#define TEXT_SM   0.45f
#define TEXT_BASE 0.50f
#define TEXT_LG   0.60f
#define TEXT_XL   0.70f
#define TEXT_2XL  0.80f
// Screen dims
#define SCREEN_TOP_W 400
#define SCREEN_TOP_H 240
#define SCREEN_BOT_W 320
#define SCREEN_BOT_H 240
