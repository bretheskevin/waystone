#ifndef WAYSTONE_SETUP_MODEL_H
#define WAYSTONE_SETUP_MODEL_H
#include <cstdio>
#include <string>

// Setup / unlock wizard text shared by the Switch and 3DS shells (plain ASCII: 3DS system font).

// Last setup step: create or join. Create/Join are encrypted; the *Plain modes have no passphrase.
enum class SetupVaultMode { Create, Join, CreatePlain, JoinPlain };

inline bool setup_mode_is_plain(SetupVaultMode mode) {
    return mode == SetupVaultMode::CreatePlain || mode == SetupVaultMode::JoinPlain;
}

inline bool setup_mode_is_join(SetupVaultMode mode) {
    return mode == SetupVaultMode::Join || mode == SetupVaultMode::JoinPlain;
}

// Only a newly created encrypted vault has a recovery key to show; every other mode goes on.
inline bool setup_mode_shows_recovery_key(SetupVaultMode mode) {
    return mode == SetupVaultMode::Create;
}

// Create <-> CreatePlain (logged); join modes follow the server and never toggle.
inline SetupVaultMode setup_toggle_encryption(SetupVaultMode mode) {
    SetupVaultMode next = mode == SetupVaultMode::CreatePlain ? SetupVaultMode::Create
                        : mode == SetupVaultMode::Create      ? SetupVaultMode::CreatePlain
                                                              : mode;
    if (next != mode)
        printf("[ui] setup: encryption toggled -> %s\n", setup_mode_is_plain(next) ? "off" : "on");
    return next;
}

// Vault step after a successful probe: join in the server's mode, or create keeping an
// encryption choice made on an earlier visit (plain by default).
inline SetupVaultMode setup_mode_after_probe(bool server_has_keys, bool server_plain,
                                             SetupVaultMode current) {
    if (server_has_keys) return server_plain ? SetupVaultMode::JoinPlain : SetupVaultMode::Join;
    return current == SetupVaultMode::Create ? SetupVaultMode::Create : SetupVaultMode::CreatePlain;
}

inline std::string setup_probe_error(int http_status) {
    std::string msg = "Can't reach the server - check the URL and login";
    if (http_status >= 0) {
        char buf[24];
        snprintf(buf, sizeof buf, " (HTTP %d)", http_status);
        msg += buf;
    }
    return msg;
}

// A probe failure with a specific reason (e.g. unrecognized keys.json) shows that reason.
inline std::string setup_probe_error(int http_status, const std::string& detail) {
    return detail.empty() ? setup_probe_error(http_status) : detail;
}

inline const char* setup_finish_label(SetupVaultMode mode, bool busy) {
    switch (mode) {
    case SetupVaultMode::Join:        return busy ? "Joining vault..." : "Join";
    case SetupVaultMode::CreatePlain: return busy ? "Setting up..." : "Finish";
    case SetupVaultMode::JoinPlain:   return busy ? "Joining..." : "Join";
    default:                          return busy ? "Creating vault..." : "Create Vault";
    }
}

inline const char* setup_probe_status() { return "Checking server..."; }

inline const char* setup_work_status(SetupVaultMode mode) {
    switch (mode) {
    case SetupVaultMode::Join:        return "Joining vault... please wait";
    case SetupVaultMode::CreatePlain: return "Setting up... please wait";
    case SetupVaultMode::JoinPlain:   return "Joining server... please wait";
    default:                          return "Creating vault... please wait";
    }
}

inline const char* setup_vault_step_title(SetupVaultMode mode) {
    switch (mode) {
    case SetupVaultMode::Join:        return "Join your vault";
    case SetupVaultMode::CreatePlain: return "Encryption";
    case SetupVaultMode::JoinPlain:   return "Join your server";
    default:                          return "Vault Passphrase";
    }
}

// Join (encrypted) returns "": that step's field uses vault_secret_hint.
inline std::string setup_vault_step_hint(SetupVaultMode mode, const char* toggle_glyph) {
    switch (mode) {
    case SetupVaultMode::CreatePlain:
        return std::string("Saves are stored unencrypted, protected by your WebDAV login (") +
               toggle_glyph + " to encrypt with a passphrase)";
    case SetupVaultMode::Create:
        return std::string("choose a strong passphrase to encrypt your saves (") + toggle_glyph +
               " for no encryption)";
    case SetupVaultMode::JoinPlain:
        return "This server stores saves unencrypted - no passphrase needed";
    default:
        return "";
    }
}

inline const char* setup_passphrase_label() { return "Passphrase"; }
inline const char* setup_confirm_label() { return "Confirm Passphrase"; }
inline const char* setup_confirm_hint() { return "re-enter your vault passphrase"; }
inline const char* setup_passphrase_mismatch() { return "Passphrases do not match"; }
inline const char* setup_confirm_required() { return "Please confirm your passphrase"; }

inline const char* recovery_key_warning() {
    return "If you forget your passphrase, this is the ONLY way to recover your vault.";
}

// Unlock screen: a plain server only needs the WebDAV password ("Log in").
inline const char* unlock_title(bool plain) { return plain ? "Log in" : "Unlock Vault"; }
inline const char* unlock_finish_label(bool plain) { return plain ? "Log in" : "Unlock"; }
inline const char* unlock_password_hint(bool plain) {
    return plain ? "Enter your WebDAV password" : "your WebDAV password";
}

inline std::string unlock_failed_message(bool plain, const char* err) {
    if (plain) return std::string("Login failed: ") + (err ? err : "invalid keys.json");
    return std::string("Unlock failed: ") + (err ? err : "wrong passphrase or key");
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
