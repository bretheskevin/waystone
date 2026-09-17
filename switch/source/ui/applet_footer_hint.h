#pragma once
#include <borealis.hpp>
#include <string>

// Clears AppletFrame debug-placeholder children from the footer and installs
// a styled hint label. Returns the label so callers can update its text later.
// Must be called AFTER AppletFrame::setContentView().
brls::Label* set_footer_hint(brls::AppletFrame* frame, const std::string& text);
