#include "remote_set.h"
#include "snapshot_browse.h"  // normalize_game_name
#include "sync_summary.h"     // href_last_segment
#include <cstdio>

extern "C" {
#include "waystone.h"
}

void remote_set_from_hrefs(const std::vector<std::string>& hrefs, const std::string& sys_seg,
                           std::set<std::string>& out) {
    out.clear();
    for (size_t i = 0; i < hrefs.size(); i++) {
        std::string seg = href_last_segment(hrefs[i]);
        if (seg.empty() || seg == sys_seg) continue;
        out.insert(seg);
    }
}

bool fetch_remote_game_set(const WsVault* vault, const WebDavCfg& dav, const char* system_name,
                           std::set<std::string>& out_game_set) {
    out_game_set.clear();
    if (!vault) {
        printf("[net] fetch_remote_game_set(%s): no vault -- filter disabled (fail-open)\n", system_name);
        return false;
    }
    char* sys_raw = ws_vault_path_segment(vault, system_name);
    if (!sys_raw) {
        printf("[net] fetch_remote_game_set: ws_vault_path_segment('%s') failed\n", system_name);
        return false;
    }
    std::string sys_str(sys_raw);
    ws_string_free(sys_raw);

    std::vector<std::string> hrefs;
    int rc = webdav_propfind(dav, sys_str.c_str(), &hrefs);
    if (rc != 0) {
        printf("[net] PROPFIND %s (%s) failed rc=%d -- remote filter disabled (fail-open)\n",
               sys_str.c_str(), system_name, rc);
        return false;
    }
    remote_set_from_hrefs(hrefs, sys_str, out_game_set);
    printf("[net] PROPFIND %s (%s) -> %zu remote game dir(s)%s\n", sys_str.c_str(), system_name,
           out_game_set.size(), hrefs.empty() ? " (404/empty)" : "");
    return true;
}

bool is_key_in_remote_set(const WsVault* vault, const std::string& game_key,
                          const std::set<std::string>& remote_games) {
    char* gseg = ws_vault_path_segment(vault, game_key.c_str());
    if (!gseg) {
        printf("[net] is_key_in_remote_set: ws_vault_path_segment('%s') failed\n", game_key.c_str());
        return false;
    }
    bool found = remote_games.count(std::string(gseg)) > 0;
    ws_string_free(gseg);
    return found;
}

bool is_in_remote_set(const WsVault* vault, const std::string& display_name,
                      const std::set<std::string>& remote_games) {
    return is_key_in_remote_set(vault, normalize_game_name(display_name), remote_games);
}
