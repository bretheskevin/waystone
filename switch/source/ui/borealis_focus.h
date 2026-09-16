#pragma once
#include <borealis.hpp>

// Walk the focus chain upward from the current focus to find which direct
// child of |box| is focused (or is an ancestor of the focused view).
// Returns the child's index, or 0 if nothing matches.
inline size_t borealis_focused_child_index(brls::Box* box) {
    auto* focus = brls::Application::getCurrentFocus();
    if (!focus) return 0;
    auto& ch = box->getChildren();
    for (size_t i = 0; i < ch.size(); i++) {
        brls::View* v = focus;
        while (v) {
            if (v == ch[i]) return i;
            v = v->getParent();
        }
    }
    return 0;
}

// Clamp |index| to [0, list_box children count - 1], then give borealis
// focus to that child.  Falls back to |fallback| when the list is empty or
// the chosen child is not focusable.  |fallback| may be nullptr.
inline void borealis_focus_child(brls::Box* list_box, size_t& index,
                                 brls::View* fallback) {
    auto& ch = list_box->getChildren();
    if (ch.empty()) {
        if (fallback)
            brls::Application::giveFocus(fallback);
        return;
    }
    if (index >= ch.size())
        index = ch.size() - 1;
    auto* row = ch[index];
    if (row->isFocusable())
        brls::Application::giveFocus(row);
    else if (fallback)
        brls::Application::giveFocus(fallback);
}
