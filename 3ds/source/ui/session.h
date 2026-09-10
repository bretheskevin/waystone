#pragma once
#include <string>
#include "net.h"
#include "wsconfig.h"
#include "secure_clear.h"

struct Vault;
typedef Vault WsVault;

// Securely overwrite a std::string's buffer before clearing.
inline void zeroize_string(std::string& s) {
    if (!s.empty()) { secure_clear(&s[0], s.size()); s.clear(); }
}

// Owns the server/user/pass strings backing the WebDavCfg pointers.
struct OwnedWebDavCfg {
    std::string server_url;
    std::string user;
    std::string pass;  // RAM-only secret

    // Positional init: server_url -> base_url, user -> user, pass -> pass.
    WebDavCfg as_cfg() const {
        return WebDavCfg{server_url.c_str(), user.c_str(), pass.c_str()};
    }

    ~OwnedWebDavCfg() { zeroize_string(pass); }

    OwnedWebDavCfg() {}
private:
    OwnedWebDavCfg(const OwnedWebDavCfg&);
    OwnedWebDavCfg& operator=(const OwnedWebDavCfg&);
};

struct Session {
    WsVault* vault;
    std::string device_id;
    OwnedWebDavCfg dav;
    WaystoneShellConfig config;
    std::string config_path;  // "sdmc:/waystone/config.json"

    Session() : vault(0) {}
private:
    Session(const Session&);
    Session& operator=(const Session&);
};
