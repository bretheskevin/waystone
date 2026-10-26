#pragma once
#include <cstdint>

// Visual spec of the JKSV-style shell (1280x720 logical). Colors are 0xRRGGBBAA.

constexpr float WS_SCREEN_W = 1280.0f;
constexpr float WS_SCREEN_H = 720.0f;

// Header
constexpr float WS_HEADER_H          = 88.0f;
constexpr float WS_MARK_X            = 66.0f;
constexpr float WS_MARK_Y            = 20.0f;
constexpr float WS_MARK_SIZE         = 48.0f;
constexpr float WS_MARK_RADIUS       = 10.0f;
constexpr float WS_MARK_GLYPH_SIZE   = 32.0f;
constexpr float WS_TITLE_X           = 130.0f;
constexpr float WS_TITLE_SIZE        = 34.0f;
constexpr float WS_STATUS_RIGHT_X    = 1250.0f;
constexpr float WS_STATUS_SIZE       = 20.0f;

// Rules
constexpr float WS_RULE_X0       = 30.0f;
constexpr float WS_RULE_X1       = 1250.0f;
constexpr float WS_RULE_TOP_Y    = 88.0f;
constexpr float WS_RULE_BOTTOM_Y = 648.0f;
constexpr float WS_RULE_W        = 1.0f;

// Version label
constexpr float WS_VERSION_X        = 8.0f;
constexpr float WS_VERSION_BASELINE = 708.0f;
constexpr float WS_VERSION_SIZE     = 14.0f;

// Guide pill
constexpr float WS_GUIDE_Y       = 662.0f;
constexpr float WS_GUIDE_H       = 48.0f;
constexpr float WS_GUIDE_RADIUS  = 24.0f;
constexpr float WS_GUIDE_INSET   = 24.0f;
constexpr float WS_GUIDE_PAD_W   = 48.0f;
constexpr float WS_GUIDE_TEXT_SIZE = 22.0f;

// Content area
constexpr float WS_CONTENT_Y = 89.0f;
constexpr float WS_CONTENT_H = 558.0f;

// Sidebar
constexpr float WS_SIDEBAR_W           = 200.0f;
constexpr float WS_SIDEBAR_ITEM_W      = 200.0f;
constexpr float WS_SIDEBAR_ITEM_H      = 120.0f;
constexpr float WS_SIDEBAR_PAD_TOP     = 16.0f;
constexpr float WS_SIDEBAR_GAP         = 8.0f;
constexpr float WS_SIDEBAR_GLYPH_SIZE  = 56.0f;
constexpr float WS_SIDEBAR_LABEL_SIZE  = 18.0f;
constexpr float WS_SIDEBAR_BAR_X       = 12.0f;
constexpr float WS_SIDEBAR_BAR_W       = 4.0f;
constexpr float WS_SIDEBAR_BAR_H       = 96.0f;
constexpr float WS_BADGE_SIZE          = 28.0f;
constexpr float WS_BADGE_TEXT_SIZE     = 16.0f;

// Pane
constexpr float WS_PANE_X              = 201.0f;
constexpr float WS_PANE_W              = WS_SCREEN_W - WS_PANE_X;
constexpr float WS_PANE_STRIP_H        = 48.0f;
constexpr float WS_PANE_NAME_SIZE      = 22.0f;
constexpr float WS_PANE_NAME_INSET     = 32.0f;
constexpr float WS_PANE_DETAIL_SIZE    = 18.0f;

// Grid
constexpr int   WS_GRID_COLS     = 7;
constexpr float WS_TILE          = 128.0f;
constexpr float WS_TILE_FOCUS    = 176.0f;
constexpr float WS_TILE_PITCH    = 144.0f;
constexpr float WS_GRID_ORIGIN   = 32.0f;

// Tile extras
constexpr float WS_TILE_BADGE_TEXT_SIZE = 14.0f;
constexpr float WS_TILE_MARKER_SIZE     = 24.0f;

// MenuList
constexpr float WS_ROW_H_SINGLE     = 40.0f;
constexpr float WS_ROW_H_DOUBLE     = 64.0f;
constexpr float WS_ROW_TITLE_SIZE   = 22.0f;
constexpr float WS_ROW_DETAIL_SIZE  = 18.0f;
constexpr float WS_ROW_INSET        = 24.0f;
constexpr float WS_ROW_BAR_X        = 8.0f;
constexpr float WS_ROW_BAR_W        = 4.0f;

// Dialog card
constexpr float WS_DIALOG_W          = 720.0f;
constexpr float WS_DIALOG_PROGRESS_W = 760.0f;
constexpr float WS_DIALOG_RADIUS     = 12.0f;
constexpr float WS_DIALOG_PAD        = 32.0f;
constexpr float WS_DIALOG_TITLE_SIZE  = 26.0f;
constexpr float WS_DIALOG_DETAIL_SIZE = 20.0f;
constexpr float WS_DIALOG_HINT_SIZE   = 20.0f;

// Progress bar
constexpr float WS_BAR_H      = 16.0f;
constexpr float WS_BAR_RADIUS = 8.0f;

// Toast
constexpr float WS_TOAST_X         = 30.0f;
constexpr float WS_TOAST_BOTTOM_Y  = 640.0f;
constexpr float WS_TOAST_H         = 44.0f;
constexpr float WS_TOAST_RADIUS    = 22.0f;
constexpr float WS_TOAST_PAD       = 20.0f;
constexpr float WS_TOAST_TEXT_SIZE = 20.0f;

// Colors
constexpr uint32_t WS_C_BG          = 0x2D2D2DFF;
constexpr uint32_t WS_C_SIDEBAR     = 0x262626FF;
constexpr uint32_t WS_C_TEXT        = 0xFFFFFFFF;
constexpr uint32_t WS_C_RULE        = 0xFFFFFFFF;
constexpr uint32_t WS_C_ACCENT      = 0x6366F1FF;
constexpr uint32_t WS_C_ACCENT_TEXT = 0x818CF8FF;
constexpr uint32_t WS_C_MUTED       = 0xA1A1AAFF;
constexpr uint32_t WS_C_DETAIL      = 0xDCDCDCFF;
constexpr uint32_t WS_C_DIALOG      = 0x505050FF;
constexpr uint32_t WS_C_DIM         = 0x00061180;
constexpr uint32_t WS_C_PANEL       = 0x000000CC;
constexpr uint32_t WS_C_GUIDE       = 0x0000007F;
constexpr uint32_t WS_C_TOAST       = 0x000000C0;
constexpr uint32_t WS_C_BADGE_BG    = 0x000000B0;
constexpr uint32_t WS_C_PLACEHOLDER = 0x383840FF;
constexpr uint32_t WS_C_BAR_TRACK   = 0x2D2D2DFF;
constexpr uint32_t WS_C_BAR         = 0x11CC33FF;
constexpr uint32_t WS_C_WARNING     = 0xF59E0BFF;
constexpr uint32_t WS_C_ERROR       = 0xEF4444FF;

// Durations (ms)
constexpr int WS_ANIM_SLIDE_MS = 200;
constexpr int WS_ANIM_TILE_MS  = 150;
constexpr int WS_TOAST_MS      = 2500;
constexpr int WS_TOAST_FADE_MS = 300;

// Material Icons (UTF-8), rendered via the regular font's material fallback.
static const char* const WS_ICON_GAMES     = "\xEE\x8C\xB8"; // U+E338 videogame_asset
static const char* const WS_ICON_EMULATOR  = "\xEE\x8C\xA2"; // U+E322 memory
static const char* const WS_ICON_CONFLICTS = "\xEE\x98\xA9"; // U+E629 sync_problem
static const char* const WS_ICON_SETTINGS  = "\xEE\xA2\xB8"; // U+E8B8 settings
static const char* const WS_ICON_SYNC      = "\xEE\x98\xA7"; // U+E627 sync
static const char* const WS_ICON_HISTORY   = "\xEE\xA2\x89"; // U+E889 history
static const char* const WS_ICON_SNAPSHOTS = "\xEE\xA1\xA4"; // U+E864 backup
static const char* const WS_ICON_WARNING   = "\xEE\x80\x82"; // U+E002 warning
static const char* const WS_ICON_ERROR     = "\xEE\x80\x80"; // U+E000 error

enum class TileMarker { None, Conflict, Failed };
