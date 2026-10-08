// Host-only test for the shared remote game-dir set.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_remote_set.cpp shell-common/remote_set.cpp \
//     shell-common/sync_summary.cpp shell-common/snapshot_browse.cpp shell-common/snapshot.cpp \
//     shell-common/file_tree.cpp -o /tmp/test_remote_set && /tmp/test_remote_set
#include "remote_set.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Vault;
extern "C" {
#include "waystone.h"
}

static int g_propfind_rc = 0;
static std::vector<std::string> g_hrefs;

extern "C" {
char* ws_vault_path_segment(const WsVault*, const char* name) {
    std::string s = std::string("obf_") + name;
    char* p = (char*)malloc(s.size() + 1);
    memcpy(p, s.c_str(), s.size() + 1);
    return p;
}
void ws_string_free(char* s) { free(s); }
}
int webdav_propfind(const WebDavCfg&, const char*, std::vector<std::string>* out) {
    *out = g_hrefs;
    return g_propfind_rc;
}

int main() {
    std::vector<std::string> hrefs;
    hrefs.push_back("/dav/obf_switch/");
    hrefs.push_back("/dav/obf_switch/obf_zelda/");
    hrefs.push_back("/dav/obf_switch/obf_mario");
    std::set<std::string> out;
    remote_set_from_hrefs(hrefs, "obf_switch", out);
    assert(out.size() == 2 && out.count("obf_zelda") && out.count("obf_mario"));

    const WsVault* fake_vault = reinterpret_cast<const WsVault*>(0x1);
    WebDavCfg cfg = {"http://x", "u", "p"};
    g_hrefs = hrefs; g_propfind_rc = 0;
    std::set<std::string> games;
    assert(fetch_remote_game_set(fake_vault, cfg, "switch", games));
    assert(games.size() == 2);
    assert(is_in_remote_set(fake_vault, "Zelda!", games));        // normalize_game_name -> "zelda"
    assert(!is_in_remote_set(fake_vault, "Metroid", games));
    assert(is_key_in_remote_set(fake_vault, "mario", games));

    g_propfind_rc = -1;                                           // transport error -> fail-open
    assert(!fetch_remote_game_set(fake_vault, cfg, "switch", games));
    assert(!fetch_remote_game_set(nullptr, cfg, "switch", games)); // no vault -> fail-open
    printf("ALL PASSED\n");
    return 0;
}
