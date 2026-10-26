#pragma once
#include <cstddef>
#include <cstdint>
#include "ws_style.h"

constexpr size_t GRID_NO_MOVE = SIZE_MAX;

enum class GridDir { Up, Down, Left, Right, PageUp, PageDown };

constexpr size_t GRID_PAGE_ROWS = 3;

inline size_t grid_rows(size_t count, size_t cols) {
    return cols ? (count + cols - 1) / cols : 0;
}

// GRID_NO_MOVE lets borealis bubble focus out of the grid (e.g. Left on column 0 -> sidebar).
inline size_t grid_move(size_t idx, GridDir d, size_t count, size_t cols) {
    if (count == 0 || cols == 0 || idx >= count) return GRID_NO_MOVE;
    const size_t last = count - 1;
    const size_t page = GRID_PAGE_ROWS * cols;
    switch (d) {
    case GridDir::Left:
        return (idx % cols == 0) ? GRID_NO_MOVE : idx - 1;
    case GridDir::Right:
        return (idx + 1 < count && (idx + 1) % cols != 0) ? idx + 1 : GRID_NO_MOVE;
    case GridDir::Up:
        return idx >= cols ? idx - cols : GRID_NO_MOVE;
    case GridDir::Down:
        if (idx + cols < count) return idx + cols;
        return (idx / cols + 1 < grid_rows(count, cols)) ? last : GRID_NO_MOVE;
    case GridDir::PageUp:
        if (idx == 0) return GRID_NO_MOVE;
        return idx > page ? idx - page : 0;
    case GridDir::PageDown:
        if (idx == last) return GRID_NO_MOVE;
        return idx + page < last ? idx + page : last;
    }
    return GRID_NO_MOVE;
}

inline void grid_cell_origin(size_t idx, size_t cols, float& x, float& y) {
    const size_t c = cols ? cols : 1;
    x = WS_GRID_ORIGIN + static_cast<float>(idx % c) * WS_TILE_PITCH;
    y = WS_GRID_ORIGIN + static_cast<float>(idx / c) * WS_TILE_PITCH;
}

inline float grid_content_width(size_t cols) {
    return 2.0f * WS_GRID_ORIGIN + static_cast<float>(cols) * WS_TILE_PITCH;
}

inline float grid_content_height(size_t count, size_t cols) {
    return 2.0f * WS_GRID_ORIGIN + static_cast<float>(grid_rows(count, cols)) * WS_TILE_PITCH;
}

constexpr float WS_TILE_FOCUS_SCALE = WS_TILE_FOCUS / WS_TILE;

// Native highlight padding that makes the highlight hug a tile drawn at |scale|.
inline float tile_highlight_padding(float scale) {
    return (scale - 1.0f) * WS_TILE * 0.5f;
}
