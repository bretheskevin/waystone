#include "swkbd_util.h"
#include "secure_clear.h"
#include <switch.h>
#include <cstring>

std::string swkbd_prompt(const char* header, const std::string& initial, bool masked) {
    SwkbdConfig config;
    swkbdCreate(&config, 0);
    swkbdConfigMakePresetDefault(&config);
    swkbdConfigSetHeaderText(&config, header);
    swkbdConfigSetInitialText(&config, initial.c_str());
    swkbdConfigSetStringLenMax(&config, 256);

    if (masked) {
        swkbdConfigSetPasswordFlag(&config, 1);
    }

    char out[257] = {};
    Result rc = swkbdShow(&config, out, sizeof(out));
    if (R_SUCCEEDED(rc) && out[0] != '\0') {
        std::string result(out);
        secure_clear(out, sizeof(out));
        swkbdClose(&config);
        return result;
    }

    secure_clear(out, sizeof(out));
    swkbdClose(&config);
    return "";
}
