// Host-only test for the updater's pure logic: version compare + release JSON parse.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common -I 3ds/source \
//     3ds/tests/test_updater.cpp 3ds/source/updater.cpp \
//     shell-common/json.cpp shell-common/base64.cpp -o /tmp/test_updater
// json.cpp is linked only because it is the SOLE jsmn implementation TU.
#include "updater.h"
#include "net.h"   // for the http_* signatures stubbed below
#include <cassert>
#include <cstdio>
#include <cstring>

// Host stubs: this test links updater.cpp WITHOUT net.cpp (curl is not
// host-available), so the two network entry points updater.cpp references
// are provided here as never-called stubs.
int http_get(const char*, std::string*) { return -1; }
int http_download(const char*, const char*,
                  bool (*)(size_t, size_t, void*), void*) { return -1; }

static void test_version_newer() {
    assert(version_newer("1.10.0", "1.9.0"));
    assert(!version_newer("1.9.0", "1.10.0"));
    assert(version_newer("0.2.0", "0.1.0"));
    assert(version_newer("2", "1.9.9"));
    assert(version_newer("1.0.1", "1.0"));
    assert(!version_newer("1.0.0", "1.0.0"));
    assert(!version_newer("1.0", "1.0.0"));
    assert(!version_newer("1.0.0", "1.0"));
    assert(!version_newer("1.0.0", "1.0.0.0"));
    assert(!version_newer("abc", "1.0.0"));   // malformed component -> 0
    assert(!version_newer("", ""));
    assert(!version_newer("1.0a", "1.0"));
    printf("test_version_newer PASSED\n");
}

static const char* kReleaseJson =
    "{\"url\":\"https://api.github.com/repos/bretheskevin/waystone/releases/1\","
    "\"tag_name\":\"v0.2.0\","
    "\"assets\":["
    "{\"name\":\"waystone.cia\","
    "\"browser_download_url\":\"https://example.com/waystone.cia\"},"
    "{\"name\":\"waystone-3ds.3dsx\","
    "\"browser_download_url\":\"https://example.com/dl/waystone-3ds.3dsx\"}"
    "],"
    "\"body\":\"release notes with \\\"escapes\\\"\"}";

static void test_parse_release_picks_3dsx() {
    char ver[64];
    char url[512];
    assert(updater_parse_release_json(kReleaseJson, ver, sizeof(ver),
                                      url, sizeof(url)) == UP_OK);
    assert(strcmp(ver, "0.2.0") == 0);  // leading 'v' stripped
    assert(strcmp(url, "https://example.com/dl/waystone-3ds.3dsx") == 0);
    printf("test_parse_release_picks_3dsx PASSED\n");
}

static void test_parse_release_no_asset() {
    char ver[64];
    char url[512];
    const char* j = "{\"tag_name\":\"v0.2.0\",\"assets\":[]}";
    assert(updater_parse_release_json(j, ver, sizeof(ver), url, sizeof(url))
           == UP_NO_ASSET);
    printf("test_parse_release_no_asset PASSED\n");
}

static void test_parse_release_bad_json() {
    char ver[64];
    char url[512];
    assert(updater_parse_release_json("not json", ver, sizeof(ver),
                                      url, sizeof(url)) == UP_PARSE);
    assert(updater_parse_release_json("{\"tag_name\":\"v1.0.0\"}", ver,
                                      sizeof(ver), url, sizeof(url))
           == UP_NO_ASSET);
    assert(updater_parse_release_json("{\"assets\":[]}", ver, sizeof(ver),
                                      url, sizeof(url)) == UP_PARSE);
    printf("test_parse_release_bad_json PASSED\n");
}

int main() {
    test_version_newer();
    test_parse_release_picks_3dsx();
    test_parse_release_no_asset();
    test_parse_release_bad_json();
    printf("All updater tests PASSED\n");
    return 0;
}
