#pragma once
#include <string>
// libctru software keyboard (blocking, takes over both screens). Empty string if cancelled.
// Secret fields have their local buffer zeroized after copying.
std::string swkbd_prompt(const char* hint_text, bool is_secret, const std::string& initial);
