#pragma once
#include <string>
#include "net.h"
#include "saves.h"
#include "wsconfig.h"
#include "secure_clear.h"

struct Vault;
typedef Vault WsVault;

// Securely overwrite a std::string's buffer before clearing.
inline void zeroize_string(std::string& s) {
    if (!s.empty()) {
        secure_clear(&s[0], s.size());
        s.clear();
    }
}

// Owns the server/user/pass strings backing the WebDavCfg pointers.
struct OwnedWebDavCfg {
    std::string server_url;
    std::string user;
    std::string pass;  // RAM-only secret

    WebDavCfg as_cfg() const {
        return WebDavCfg{server_url.c_str(), user.c_str(), pass.c_str()};
    }

    ~OwnedWebDavCfg() { zeroize_string(pass); }

    OwnedWebDavCfg() = default;
    OwnedWebDavCfg(const OwnedWebDavCfg&) = delete;
    OwnedWebDavCfg& operator=(const OwnedWebDavCfg&) = delete;
    OwnedWebDavCfg(OwnedWebDavCfg&&) = delete;
    OwnedWebDavCfg& operator=(OwnedWebDavCfg&&) = delete;
};

struct Session {
    WsVault* vault = nullptr;
    AccountUid uid = {};
    std::string device_id;
    OwnedWebDavCfg dav;
    WaystoneShellConfig config;
    std::string config_path;  // e.g. "sdmc:/waystone/config.json"

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};
