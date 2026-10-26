#pragma once
#include <cstdint>
#include "ws_style.h"

// Toast opacity for a given age: opaque, then a linear fade over the last WS_TOAST_FADE_MS.
inline float ws_toast_alpha(int64_t elapsed_ms) {
    const int64_t fade_start = WS_TOAST_MS - WS_TOAST_FADE_MS;
    if (elapsed_ms < fade_start) return 1.0f;
    if (elapsed_ms >= WS_TOAST_MS) return 0.0f;
    return static_cast<float>(WS_TOAST_MS - elapsed_ms) / static_cast<float>(WS_TOAST_FADE_MS);
}
