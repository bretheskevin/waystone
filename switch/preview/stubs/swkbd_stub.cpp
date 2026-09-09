/*
 * Preview stub for swkbd_util — replaces switch/source/ui/swkbd_util.cpp.
 * Returns canned realistic strings instead of invoking the libnx software keyboard.
 */
#include "swkbd_util.h"
#include <string>

std::string swkbd_prompt(const char* header, const std::string& initial, bool masked)
{
    (void)masked;

    // If the caller pre-filled a real value, return it as-is.
    if (!initial.empty() && initial != "not set")
        return initial;

    // Otherwise return a step-appropriate canned value.
    if (!header) return "preview-value";
    std::string h(header);
    if (h == "Server URL")          return "https://dav.example.com";
    if (h == "Username")            return "waystone";
    if (h == "WebDAV Password")     return "hunter2";
    if (h == "Vault Passphrase")    return "correct-horse-battery-staple";
    if (h == "Confirm Passphrase")  return "correct-horse-battery-staple";
    if (h == "Recovery Key")        return "DEADBEEF00112233445566778899AABB";
    return "preview-value";
}
