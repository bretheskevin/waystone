#pragma once
#include <string>
#include <vector>

struct Session;

using PreviewModeFn = void (*)(Session&);

void preview_register(const char* name, PreviewModeFn fn);
PreviewModeFn preview_find(const char* name);
std::vector<std::string> preview_mode_names();

// Self-registration from a mode TU: `static PreviewModeReg reg_x("x", &run_x);`
struct PreviewModeReg {
    PreviewModeReg(const char* name, PreviewModeFn fn) { preview_register(name, fn); }
};
