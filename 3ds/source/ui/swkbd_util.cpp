#include "swkbd_util.h"
#include "secure_clear.h"   // shell-common; on INCLUDES path
#include <3ds.h>
#include <cstring>

std::string swkbd_prompt(const char* hint_text, bool is_secret, const std::string& initial) {
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 256);
    swkbdSetHintText(&swkbd, hint_text ? hint_text : "");
    if (is_secret) swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE);
    if (!initial.empty()) swkbdSetInitialText(&swkbd, initial.c_str());
    char buf[257]; memset(buf, 0, sizeof(buf));
    SwkbdButton pressed = swkbdInputText(&swkbd, buf, sizeof(buf));
    std::string result;
    if (pressed == SWKBD_BUTTON_RIGHT && buf[0] != '\0') result = buf;
    secure_clear(buf, sizeof(buf));
    return result;
}
