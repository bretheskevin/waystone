// Host-only test for the overlay pop settle math.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I switch/source/ui/kit \
//     switch/tests/test_overlay_timing.cpp -o /tmp/test_overlay_timing && /tmp/test_overlay_timing
#include "overlay_timing.h"
#include <cassert>
#include <cstdio>

static void test_settle_covers_click_pulse() {
    // click pulse = highlight forward + highlight reverse
    assert(ws_overlay_settle_ms(100.0f, 100.0f) >= 200);
    assert(ws_overlay_settle_ms(100.0f, 300.0f) >= 600);
    printf("test_settle_covers_click_pulse PASSED\n");
}

static void test_settle_covers_show_animation() {
    assert(ws_overlay_settle_ms(250.0f, 100.0f) >= 250);
    printf("test_settle_covers_show_animation PASSED\n");
}

static void test_settle_has_frame_margin() {
    assert(ws_overlay_settle_ms(250.0f, 100.0f) > 250);
    assert(ws_overlay_settle_ms(0.0f, 0.0f) > 0);
    printf("test_settle_has_frame_margin PASSED\n");
}

static void test_remaining_counts_down_and_clamps() {
    assert(ws_overlay_remaining_ms(300, 0) == 300);
    assert(ws_overlay_remaining_ms(300, 200) == 100);
    assert(ws_overlay_remaining_ms(300, 300) == 0);
    assert(ws_overlay_remaining_ms(300, 5000) == 0);
    printf("test_remaining_counts_down_and_clamps PASSED\n");
}

int main() {
    test_settle_covers_click_pulse();
    test_settle_covers_show_animation();
    test_settle_has_frame_margin();
    test_remaining_counts_down_and_clamps();
    printf("ALL PASSED\n");
    return 0;
}
