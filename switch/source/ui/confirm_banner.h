#pragma once
#include <borealis.hpp>
#include <string>

// Amber confirm banner: title, optional detail line, then the Confirm/Cancel glyph hint.
brls::Box* make_confirm_banner(const std::string& title, const std::string& detail);
