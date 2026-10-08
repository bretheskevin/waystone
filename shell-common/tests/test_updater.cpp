// Host-only test for the updater's pure logic: version compare + release JSON parse.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common \
//     shell-common/tests/test_updater.cpp shell-common/updater.cpp \
//     shell-common/json.cpp shell-common/base64.cpp -o /tmp/test_updater && /tmp/test_updater
// json.cpp is linked only because it is the SOLE jsmn implementation TU.
#include "updater.h"
#include "net.h"   // for the http_* signatures stubbed below
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

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
    assert(updater_parse_release_json(kReleaseJson, ".3dsx", ver, sizeof(ver),
                                      url, sizeof(url)) == UP_OK);
    assert(strcmp(ver, "0.2.0") == 0);  // leading 'v' stripped
    assert(strcmp(url, "https://example.com/dl/waystone-3ds.3dsx") == 0);
    printf("test_parse_release_picks_3dsx PASSED\n");
}

// GitHub lists the .3dsx before the .cia on real releases; a later asset must
// not overwrite the chosen .3dsx URL.
static void test_parse_release_3dsx_before_cia() {
    const char* j =
        "{\"tag_name\":\"v0.4.0\",\"assets\":["
        "{\"browser_download_url\":\"https://example.com/dl/waystone-3ds.3dsx\"},"
        "{\"browser_download_url\":\"https://example.com/dl/waystone-3ds.cia\"}"
        "]}";
    char ver[64];
    char url[512];
    assert(updater_parse_release_json(j, ".3dsx", ver, sizeof(ver), url, sizeof(url)) == UP_OK);
    assert(strcmp(url, "https://example.com/dl/waystone-3ds.3dsx") == 0);
    printf("test_parse_release_3dsx_before_cia PASSED\n");
}

static void test_parse_release_no_asset() {
    char ver[64];
    char url[512];
    const char* j = "{\"tag_name\":\"v0.2.0\",\"assets\":[]}";
    assert(updater_parse_release_json(j, ".3dsx", ver, sizeof(ver), url, sizeof(url))
           == UP_NO_ASSET);
    printf("test_parse_release_no_asset PASSED\n");
}

static void test_parse_release_bad_json() {
    char ver[64];
    char url[512];
    assert(updater_parse_release_json("not json", ".3dsx", ver, sizeof(ver),
                                      url, sizeof(url)) == UP_PARSE);
    assert(updater_parse_release_json("{\"tag_name\":\"v1.0.0\"}", ".3dsx", ver,
                                      sizeof(ver), url, sizeof(url))
           == UP_NO_ASSET);
    assert(updater_parse_release_json("{\"assets\":[]}", ".3dsx", ver, sizeof(ver),
                                      url, sizeof(url)) == UP_PARSE);
    printf("test_parse_release_bad_json PASSED\n");
}

static const char* kBothAssets =
    "{\"tag_name\":\"v0.5.0\",\"assets\":["
    "{\"name\":\"waystone.nro\",\"browser_download_url\":\"https://x/waystone.nro\"},"
    "{\"name\":\"waystone-3ds-spike.3dsx\",\"browser_download_url\":\"https://x/waystone-3ds-spike.3dsx\"},"
    "{\"name\":\"waystone-3ds-spike.cia\",\"browser_download_url\":\"https://x/waystone-3ds-spike.cia\"}]}";

static void test_suffix_selects_asset() {
    char ver[32], url[256];
    assert(updater_parse_release_json(kBothAssets, ".3dsx", ver, sizeof ver, url, sizeof url) == UP_OK);
    assert(strcmp(ver, "0.5.0") == 0 && strcmp(url, "https://x/waystone-3ds-spike.3dsx") == 0);
    assert(updater_parse_release_json(kBothAssets, ".nro", ver, sizeof ver, url, sizeof url) == UP_OK);
    assert(strcmp(url, "https://x/waystone.nro") == 0);
    const char* only3ds = "{\"tag_name\":\"v1.0.0\",\"assets\":[{\"browser_download_url\":\"https://x/a.3dsx\"}]}";
    assert(updater_parse_release_json(only3ds, ".nro", ver, sizeof ver, url, sizeof url) == UP_NO_ASSET);
    assert(updater_check_message(UP_NO_ASSET, ".nro") == "Latest release has no .nro build");
    assert(updater_install_message(UP_NO_SELF) == "Relaunch from SD card to enable updates");
    assert(updater_parse_release_json(kBothAssets, ".cia", ver, sizeof ver, url, sizeof url) == UP_OK);
    assert(strcmp(url, "https://x/waystone-3ds-spike.cia") == 0);
    assert(updater_install_message(UP_INSTALL) ==
           "Install failed \xe2\x80\x94 current version kept");
    printf("test_suffix_selects_asset PASSED\n");
}

int main() {
    test_version_newer();
    test_parse_release_picks_3dsx();
    test_parse_release_3dsx_before_cia();
    test_parse_release_no_asset();
    test_parse_release_bad_json();
    test_suffix_selects_asset();
    printf("All updater tests PASSED\n");
    return 0;
}
