#ifndef WAYSTONE_WSCONFIG_H
#define WAYSTONE_WSCONFIG_H

#include <string>

// Conflict resolution policy (mirrors core::conflict::ConflictPolicy).
// Values match the int parameter to ws_decide_pull: 0 = NewestWins, 1 = Prompt.
enum class WsConflictPolicy { NewestWins = 0, Prompt = 1 };

struct WaystoneShellConfig {
    std::string server_url;
    std::string username;
    WsConflictPolicy conflict_policy;
    bool safety_backup;

    WaystoneShellConfig()
        : conflict_policy(WsConflictPolicy::NewestWins),
          safety_backup(true) {}
};

// Load config from a JSON file at `path`.
// Returns default config if the file does not exist or cannot be parsed.
WaystoneShellConfig wsconfig_load(const char* path);

// Save config to a JSON file at `path`.
// Returns true on success, false on I/O error.
bool wsconfig_save(const WaystoneShellConfig& cfg, const char* path);

#endif
