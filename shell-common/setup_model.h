#ifndef WAYSTONE_SETUP_MODEL_H
#define WAYSTONE_SETUP_MODEL_H
#include <cstdio>
#include <string>

// Setup / unlock wizard text shared by the Switch and 3DS shells (plain ASCII: 3DS system font).

// Last setup step: create a new vault, or join the one already on the server.
enum class SetupVaultMode { Create, Join };

inline std::string setup_probe_error(int http_status) {
    std::string msg = "Can't reach the server - check the URL and login";
    if (http_status >= 0) {
        char buf[24];
        snprintf(buf, sizeof buf, " (HTTP %d)", http_status);
        msg += buf;
    }
    return msg;
}

inline const char* setup_finish_label(SetupVaultMode mode, bool busy) {
    if (mode == SetupVaultMode::Join) return busy ? "Joining vault..." : "Join";
    return busy ? "Creating vault..." : "Create Vault";
}

inline const char* setup_probe_status() { return "Checking server..."; }

inline const char* setup_work_status(SetupVaultMode mode) {
    return mode == SetupVaultMode::Join ? "Joining vault... please wait"
                                        : "Creating vault... please wait";
}

inline const char* setup_vault_step_title(SetupVaultMode mode) {
    return mode == SetupVaultMode::Join ? "Join your vault" : "Vault Passphrase";
}

// Vault secret field (unlock + setup join): passphrase, or the recovery key when `recovery`.
inline const char* vault_secret_label(bool recovery) {
    return recovery ? "Recovery Key" : "Vault Passphrase";
}

inline std::string vault_secret_hint(bool recovery, const char* toggle_glyph) {
    return recovery
        ? std::string("enter your hex recovery key (") + toggle_glyph + " to switch to passphrase)"
        : std::string("enter your vault passphrase (") + toggle_glyph + " to switch to recovery key)";
}

inline const char* vault_secret_required(bool recovery) {
    return recovery ? "Recovery key is required" : "Passphrase is required";
}

#endif
