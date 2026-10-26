// Host-only test for the kit toast fade math.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I switch/source/ui/kit \
//     switch/tests/test_toast.cpp -o /tmp/test_toast && /tmp/test_toast
#include "toast_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b) { return std::fabs(a - b) <= 0.01f; }

static void test_fully_visible_before_fade() {
    assert(ws_toast_alpha(0) == 1.0f);
    assert(ws_toast_alpha(2199) == 1.0f);
    printf("test_fully_visible_before_fade PASSED\n");
}

static void test_linear_fade() {
    assert(near(ws_toast_alpha(2350), 0.5f));
    printf("test_linear_fade PASSED\n");
}

static void test_gone_after_end() {
    assert(ws_toast_alpha(2500) == 0.0f);
    assert(ws_toast_alpha(9999) == 0.0f);
    printf("test_gone_after_end PASSED\n");
}

static void test_negative_elapsed_is_visible() {
    assert(ws_toast_alpha(-5) == 1.0f);
    printf("test_negative_elapsed_is_visible PASSED\n");
}

int main() {
    test_fully_visible_before_fade();
    test_linear_fade();
    test_gone_after_end();
    test_negative_elapsed_is_visible();
    printf("ALL PASSED\n");
    return 0;
}
