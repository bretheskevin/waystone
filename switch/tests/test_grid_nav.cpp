// Host-only test for the kit grid navigation math.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I switch/source/ui/kit \
//     switch/tests/test_grid_nav.cpp -o /tmp/test_grid_nav && /tmp/test_grid_nav
#include "grid_nav.h"
#include <cassert>
#include <cstdio>

static const size_t N = 17;
static const size_t C = 7;

static size_t mv(size_t idx, GridDir d) { return grid_move(idx, d, N, C); }

static void test_left() {
    assert(mv(0, GridDir::Left) == GRID_NO_MOVE);
    assert(mv(7, GridDir::Left) == GRID_NO_MOVE);
    assert(mv(8, GridDir::Left) == 7);
    printf("test_left PASSED\n");
}

static void test_right() {
    assert(mv(6, GridDir::Right) == GRID_NO_MOVE);
    assert(mv(16, GridDir::Right) == GRID_NO_MOVE);
    assert(mv(15, GridDir::Right) == 16);
    printf("test_right PASSED\n");
}

static void test_up() {
    assert(mv(3, GridDir::Up) == GRID_NO_MOVE);
    assert(mv(10, GridDir::Up) == 3);
    printf("test_up PASSED\n");
}

static void test_down() {
    assert(mv(3, GridDir::Down) == 10);
    assert(mv(9, GridDir::Down) == 16);
    assert(mv(12, GridDir::Down) == 16);
    assert(mv(14, GridDir::Down) == GRID_NO_MOVE);
    assert(mv(16, GridDir::Down) == GRID_NO_MOVE);
    printf("test_down PASSED\n");
}

static void test_page() {
    assert(mv(0, GridDir::PageDown) == 16);
    assert(mv(16, GridDir::PageDown) == GRID_NO_MOVE);
    assert(mv(16, GridDir::PageUp) == 0);
    assert(mv(0, GridDir::PageUp) == GRID_NO_MOVE);
    printf("test_page PASSED\n");
}

static void test_empty_grid() {
    const GridDir all[] = { GridDir::Up, GridDir::Down, GridDir::Left, GridDir::Right,
                            GridDir::PageUp, GridDir::PageDown };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        assert(grid_move(0, all[i], 0, C) == GRID_NO_MOVE);
    printf("test_empty_grid PASSED\n");
}

static void test_rows() {
    assert(grid_rows(17, 7) == 3);
    assert(grid_rows(0, 7) == 0);
    assert(grid_rows(7, 7) == 1);
    printf("test_rows PASSED\n");
}

static void test_cell_origin() {
    float x = 0, y = 0;
    grid_cell_origin(8, 7, x, y);
    assert(x == 32.0f + 144.0f);
    assert(y == 32.0f + 144.0f);
    grid_cell_origin(0, 7, x, y);
    assert(x == 32.0f && y == 32.0f);
    printf("test_cell_origin PASSED\n");
}

static void test_content_size() {
    assert(grid_content_width(7) == 32.0f + 7 * 144.0f + 32.0f);
    assert(grid_content_height(24, 7) == 32.0f + 4 * 144.0f + 32.0f);
    assert(grid_content_height(0, 7) == 64.0f);
    printf("test_content_size PASSED\n");
}

static void test_tile_focus_scale() {
    assert(WS_TILE_FOCUS_SCALE == 176.0f / 128.0f);
    assert(tile_highlight_padding(1.0f) == 0.0f);
    assert(tile_highlight_padding(WS_TILE_FOCUS_SCALE) == 24.0f);
    printf("test_tile_focus_scale PASSED\n");
}

int main() {
    test_left();
    test_right();
    test_up();
    test_down();
    test_page();
    test_empty_grid();
    test_rows();
    test_cell_origin();
    test_content_size();
    test_tile_focus_scale();
    printf("ALL grid_nav tests PASSED\n");
    return 0;
}
