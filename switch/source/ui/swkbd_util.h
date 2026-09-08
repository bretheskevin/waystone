#pragma once
#include <string>

// Show the libnx software keyboard and return the entered text.
// header: prompt text shown above the input field.
// initial: pre-filled text.
// masked: if true, characters are hidden (password mode).
// Returns the entered string, or empty string if the user cancelled.
std::string swkbd_prompt(const char* header, const std::string& initial, bool masked);
