#pragma once
#include <cstdint>

// borealis deletes a popped activity while Ticking::updateTickings() walks a copy of the running
// list, so every Animatable inside the overlay must have finished before the pop: the A click
// pulse (highlight forward + reverse), focus highlight fades, the show/hide fade. Plus 2 frames.
inline int64_t ws_overlay_settle_ms(float show_ms, float highlight_ms) {
    const float click_ms = 2.0f * highlight_ms;
    const float longest  = show_ms > click_ms ? show_ms : click_ms;
    return static_cast<int64_t>(longest) + 34;
}

inline int64_t ws_overlay_remaining_ms(int64_t settle_ms, int64_t elapsed_ms) {
    return elapsed_ms >= settle_ms ? 0 : settle_ms - elapsed_ms;
}
