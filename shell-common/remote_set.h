#ifndef WAYSTONE_REMOTE_SET_H
#define WAYSTONE_REMOTE_SET_H

#include <set>
#include <string>
#include <vector>
#include "net.h"

struct Vault;
typedef Vault WsVault;

// Child segments of a Depth-1 PROPFIND of collection `sys_seg` (self entry skipped).
void remote_set_from_hrefs(const std::vector<std::string>& hrefs, const std::string& sys_seg,
                           std::set<std::string>& out);

// One PROPFIND of the obfuscated <system> collection -> obfuscated game dir names.
// true when the PROPFIND completed (404/empty is valid); false on transport error or no vault:
// callers MUST fail-open (show everything, has_remote = true).
bool fetch_remote_game_set(const WsVault* vault, const WebDavCfg& dav, const char* system_name,
                           std::set<std::string>& out_game_set);

bool is_key_in_remote_set(const WsVault* vault, const std::string& game_key,
                          const std::set<std::string>& remote_games);

// game key = normalize_game_name(display_name) -- exactly how push stores the game dir.
bool is_in_remote_set(const WsVault* vault, const std::string& display_name,
                      const std::set<std::string>& remote_games);

#endif
