// Host-only test.
#include "wsconfig.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void test_round_trip() {
    const char* path = "/tmp/waystone_test_config.json";
    WaystoneShellConfig cfg;
    cfg.server_url = "https://dav.example.com/waystone";
    cfg.username = "alice";
    cfg.conflict_policy = WsConflictPolicy::Prompt;
    cfg.safety_backup = false;
    assert(wsconfig_save(cfg, path));
    WaystoneShellConfig loaded = wsconfig_load(path);
    assert(loaded.server_url == "https://dav.example.com/waystone");
    assert(loaded.username == "alice");
    assert(loaded.conflict_policy == WsConflictPolicy::Prompt);
    assert(!loaded.safety_backup);
    remove(path);
    printf("test_round_trip PASSED\n");
}

static void test_default_on_missing_file() {
    WaystoneShellConfig cfg = wsconfig_load("/tmp/nonexistent_waystone_config.json");
    assert(cfg.server_url.empty());
    assert(cfg.username.empty());
    assert(cfg.conflict_policy == WsConflictPolicy::NewestWins);
    assert(cfg.safety_backup == true);
    printf("test_default_on_missing_file PASSED\n");
}

static void test_escape_special_chars() {
    const char* path = "/tmp/waystone_test_config_esc.json";
    WaystoneShellConfig cfg;
    cfg.server_url = "https://example.com/path?a=1&b=\"two\"";
    cfg.username = "user\\name";
    assert(wsconfig_save(cfg, path));
    WaystoneShellConfig loaded = wsconfig_load(path);
    assert(loaded.server_url == cfg.server_url);
    assert(loaded.username == cfg.username);
    remove(path);
    printf("test_escape_special_chars PASSED\n");
}

int main() {
    test_round_trip();
    test_default_on_missing_file();
    test_escape_special_chars();
    printf("All wsconfig tests PASSED\n");
    return 0;
}
