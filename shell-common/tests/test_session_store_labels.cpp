// Host-only test for the Settings WebDAV-password row texts (session_store.h inline helpers).
// Build (from repo root):
//   g++ -std=c++11 -Wall -Wextra -I shell-common shell-common/tests/test_session_store_labels.cpp \
//     -o /tmp/test_session_store_labels && /tmp/test_session_store_labels
#include "session_store.h"
#include <cassert>
#include <cstdio>

static const char* BULLETS = "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
                             "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";

static void test_mask_hides_secret() {
    assert(webdav_pass_mask("hunter2") == BULLETS);
    assert(webdav_pass_mask("") == "not set");
    printf("test_mask_hides_secret PASSED\n");
}

static void test_label_prefixes_mask() {
    assert(webdav_pass_label("hunter2") == std::string("WebDAV password: ") + BULLETS);
    assert(webdav_pass_label("") == "WebDAV password: not set");
    printf("test_label_prefixes_mask PASSED\n");
}

int main() {
    test_mask_hides_secret();
    test_label_prefixes_mask();
    return 0;
}
