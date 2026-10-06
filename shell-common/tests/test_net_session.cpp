// Host-only test for WebDavSession connection reuse.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common \
//     shell-common/tests/test_net_session.cpp shell-common/net.cpp \
//     -o /tmp/test_net_session -lcurl && /tmp/test_net_session
// Requires a local dufs server (same as the desktop webdav tests):
//   docker run --rm -d --name waystone-dufs-test -p 5099:5000 \
//     -v /tmp/waystone-dufs-test-data:/data sigoden/dufs:v0.43.0 /data --allow-all
// Skips with a printed notice (exit 0) when the server is unreachable.
// Re-runs are idempotent: MKCOL 405 maps to 0, PUT overwrites.
#include "net.h"
#include <curl/curl.h>
#include <cassert>
#include <cstdio>
#include <cstring>

static const char* BASE = "http://localhost:5099";

static WebDavCfg test_cfg() {
    WebDavCfg cfg = { BASE, nullptr, nullptr };
    return cfg;
}

static bool server_reachable() {
    WebDavCfg cfg = test_cfg();
    std::vector<std::string> hrefs;
    // PROPFIND the root; transport error (-1) = server down. 404/2xx both mean "up".
    return webdav_propfind(cfg, "", &hrefs) != -1;
}

static void test_session_round_trip() {
    WebDavCfg cfg = test_cfg();
    WebDavSession* s = webdav_session_begin(cfg);
    assert(s != nullptr);

    assert(webdav_mkdir_p_s(s, "ws-net-session-test/sub") == 0);

    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert(webdav_put_s(s, "ws-net-session-test/blob.bin",
                        payload, sizeof(payload)) == 0);

    std::vector<uint8_t> out;
    assert(webdav_get_s(s, "ws-net-session-test/blob.bin", &out) == 0);
    assert(out.size() == sizeof(payload));
    assert(memcmp(out.data(), payload, sizeof(payload)) == 0);

    assert(webdav_exists_s(s, "ws-net-session-test/blob.bin") == 1);
    assert(webdav_exists_s(s, "ws-net-session-test/nope.bin") == 0);

    // 404 semantics through a session: rc 1, out cleared (None).
    assert(webdav_get_s(s, "ws-net-session-test/nope.bin", &out) == 1);
    assert(out.empty());

    std::vector<std::string> hrefs;
    assert(webdav_propfind_s(s, "ws-net-session-test", &hrefs) == 0);
    assert(!hrefs.empty());

    // The whole point: all of the above (8 transfers) reused one TCP connection.
    assert(webdav_session_num_connects(s) == 1);

    webdav_session_end(s);
    printf("test_session_round_trip PASSED\n");
}

static void test_no_option_leak_after_mkcol_and_head() {
    WebDavCfg cfg = test_cfg();
    WebDavSession* s = webdav_session_begin(cfg);
    assert(s != nullptr);

    const uint8_t payload[] = {0x01, 0x02, 0x03};
    assert(webdav_put_s(s, "ws-net-session-test/leak.bin",
                        payload, sizeof(payload)) == 0);

    // mkcol sets CUSTOMREQUEST "MKCOL"; exists sets NOBODY. A following GET must
    // still be a real GET with a body (guards the curl_easy_reset discipline).
    assert(webdav_mkcol_s(s, "ws-net-session-test/leakdir") == 0);
    assert(webdav_exists_s(s, "ws-net-session-test/leak.bin") == 1);

    std::vector<uint8_t> out;
    assert(webdav_get_s(s, "ws-net-session-test/leak.bin", &out) == 0);
    assert(out.size() == sizeof(payload));
    assert(memcmp(out.data(), payload, sizeof(payload)) == 0);

    webdav_session_end(s);
    printf("test_no_option_leak_after_mkcol_and_head PASSED\n");
}

static void test_compat_one_shot() {
    WebDavCfg cfg = test_cfg();
    const uint8_t payload[] = {0xAA};
    assert(webdav_put(cfg, "ws-net-session-test/compat.bin",
                      payload, sizeof(payload)) == 0);
    std::vector<uint8_t> out;
    assert(webdav_get(cfg, "ws-net-session-test/compat.bin", &out) == 0);
    assert(out.size() == 1 && out[0] == 0xAA);
    assert(webdav_get(cfg, "ws-net-session-test/definitely-not-there.bin", &out) == 1);
    assert(out.empty());
    printf("test_compat_one_shot PASSED\n");
}

static void test_session_end_nullptr() {
    webdav_session_end(nullptr); // must be a no-op (no crash)
    assert(webdav_session_num_connects(nullptr) == 0);
    printf("test_session_end_nullptr PASSED\n");
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!server_reachable()) {
        printf("SKIP: no WebDAV server at %s (start dufs -- see header)\n", BASE);
        return 0;
    }
    test_session_round_trip();
    test_no_option_leak_after_mkcol_and_head();
    test_compat_one_shot();
    test_session_end_nullptr();
    printf("ALL TESTS PASSED\n");
    return 0;
}
