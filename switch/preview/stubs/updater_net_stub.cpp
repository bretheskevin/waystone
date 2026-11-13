// Preview stub for the shell-common/net.cpp HTTP calls the real updater.cpp references, plus the
// jsmn implementation (json.cpp, which normally provides it, is not compiled into the preview).
#include "net.h"
#include "jsmn.h"
#include <cstdio>

int http_get(const char* url, std::string*) {
    printf("[net] preview stub: GET %s -> transport error\n", url ? url : "(null)");
    return -1;
}

int http_download(const char* url, const char*, bool (*)(size_t, size_t, void*), void*) {
    printf("[net] preview stub: download %s -> transport error\n", url ? url : "(null)");
    return -1;
}
