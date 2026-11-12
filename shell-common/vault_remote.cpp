#include "vault_remote.h"
#include "ffi_err.h"
#include "keys_file.h"
#include "secure_clear.h"
#include "setup_model.h"
#include "snapshot.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

const char* const kKeysPath = "/keys.json";

bool is_2xx(int status) { return status >= 200 && status < 300; }

std::string with_status(const char* msg, int status) {
    char buf[160];
    snprintf(buf, sizeof buf, "%s (HTTP %d)", msg, status);
    return buf;
}

int cfg_get(void* ctx, const char* path, std::vector<uint8_t>* out) {
    int rc = webdav_get(*static_cast<const WebDavCfg*>(ctx), path, out);
    if (rc == 0) return 200;
    if (rc == 1) return 404;
    return -1; // webdav_get does not surface the status; net.cpp logs it
}

int cfg_put(void* ctx, const char* path, const uint8_t* data, size_t len) {
    int rc = webdav_put_if_absent(*static_cast<const WebDavCfg*>(ctx), path, data, len);
    return rc == 0 ? 200 : rc;
}

const char* const kVaultExists = "A vault already exists on this server - join it instead";
const char* const kUnrecognizedKeys = "Unrecognized keys.json on server";
const int kHttpPreconditionFailed = 412;

void wipe_buf(WsBuf* b) {
    if (b->ptr) secure_clear(b->ptr, b->len);
    ws_buf_free(*b);
    b->ptr = nullptr;
    b->len = 0;
}

bool write_whole_file(const std::string& path, const void* data, size_t len) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && !fs_mkdir_p(path.substr(0, slash))) {
        printf("[vault] mkdir for %s failed\n", path.c_str());
        return false;
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        printf("[vault] open %s for write failed\n", path.c_str());
        return false;
    }
    size_t wrote = fwrite(data, 1, len, f);
    bool ok = (fclose(f) == 0) && wrote == len;
    printf("[vault] write %s (%zu bytes) %s\n", path.c_str(), len, ok ? "ok" : "FAILED");
    return ok;
}

} // namespace

VaultDav vault_dav_from_cfg(const WebDavCfg& dav) {
    VaultDav d;
    d.get = cfg_get;
    d.put = cfg_put;
    d.ctx = const_cast<WebDavCfg*>(&dav);
    d.log_url = webdav_loggable_url(dav.base_url ? dav.base_url : "");
    return d;
}

RemoteKeys vault_fetch_remote_keys(const VaultDav& dav) {
    printf("[vault] probe %s%s ...\n", dav.log_url.c_str(), kKeysPath);
    RemoteKeys r;
    r.plain = false;
    r.http_status = dav.get(dav.ctx, kKeysPath, &r.bytes);
    if (r.http_status == 404) {
        r.state = RemoteKeysState::Absent;
        r.bytes.clear();
    } else if (is_2xx(r.http_status) && !r.bytes.empty()) {
        if (ffi_keys_mode(r.bytes.data(), r.bytes.size(), &r.plain)) {
            r.state = RemoteKeysState::Present;
        } else {
            printf("[vault] probe: ws_keys_mode failed (%zu bytes): %s\n", r.bytes.size(), ffi_err());
            r.state = RemoteKeysState::Error;
            r.error = kUnrecognizedKeys;
            r.bytes.clear();
        }
    } else {
        r.state = RemoteKeysState::Error;
        r.bytes.clear();
    }
    if (r.state == RemoteKeysState::Present) {
        printf("[vault] probe done: present mode=%s (http=%d, %zu bytes)\n",
               keys_mode_name(r.plain), r.http_status, r.bytes.size());
    } else {
        printf("[vault] probe done: %s (http=%d, %zu bytes)\n",
               r.state == RemoteKeysState::Absent ? "absent" : "error", r.http_status,
               r.bytes.size());
    }
    return r;
}

RemoteKeys vault_fetch_remote_keys(const WebDavCfg& dav) {
    return vault_fetch_remote_keys(vault_dav_from_cfg(dav));
}

VaultSetupResult vault_create_and_publish(const VaultDav& dav, const char* passphrase) {
    VaultSetupResult res = {nullptr, "", std::vector<uint8_t>(), ""};
    const bool plain = passphrase == nullptr;
    printf("[vault] create: start (mode=%s)\n", keys_mode_name(plain));

    WsBuf recovery = {nullptr, 0};
    WsBuf keys = {nullptr, 0};
    const char* step = plain ? "ws_vault_plain" : "ws_vault_init (Argon2)";
    printf("[vault] create: %s ...\n", step);
    WsVault* vault = plain ? ws_vault_plain() : ws_vault_init(passphrase, &recovery, &keys);
    if (vault && plain) keys = ws_vault_keys_json(vault);
    if (!vault || !keys.ptr) {
        printf("[vault] create: %s failed: %s\n", step, ffi_err());
        res.error = std::string("Vault creation failed: ") + ffi_err();
        if (vault) ws_vault_free(vault);
        wipe_buf(&recovery);
        ws_buf_free(keys);
        return res;
    }
    printf("[vault] create: %s done (keys %zu bytes)\n", step, static_cast<size_t>(keys.len));

    // Probe after the slow KDF so the probe -> PUT window stays minimal.
    RemoteKeys probe = vault_fetch_remote_keys(dav);
    int status = 0;
    if (probe.state == RemoteKeysState::Present) {
        printf("[vault] create refused: server already has a vault - discarding vault\n");
        res.error = kVaultExists;
    } else if (probe.state == RemoteKeysState::Error) {
        printf("[vault] create refused: probe failed (http=%d) - discarding vault\n",
               probe.http_status);
        res.error = !probe.error.empty()
            ? probe.error
            : with_status("Could not check the server for an existing vault", probe.http_status);
    } else {
        status = dav.put(dav.ctx, kKeysPath, keys.ptr, keys.len);
        if (!is_2xx(status)) {
            printf("[vault] create: PUT %s%s failed (http=%d) - discarding vault\n",
                   dav.log_url.c_str(), kKeysPath, status);
            res.error = status == kHttpPreconditionFailed
                ? std::string(kVaultExists)
                : with_status("Could not upload the vault to the server", status);
        }
    }
    if (!res.error.empty()) {
        ws_vault_free(vault);
        wipe_buf(&recovery);
        ws_buf_free(keys);
        return res;
    }
    printf("[vault] create: PUT %s%s ok (http=%d)\n", dav.log_url.c_str(), kKeysPath, status);

    res.vault = vault;
    res.keys_json.assign(keys.ptr, keys.ptr + keys.len);
    if (recovery.ptr && recovery.len > 0)
        res.recovery_hex.assign(reinterpret_cast<const char*>(recovery.ptr), recovery.len);
    wipe_buf(&recovery);
    ws_buf_free(keys);
    printf("[vault] create: done\n");
    return res;
}

VaultSetupResult vault_create_and_publish(const WebDavCfg& dav, const char* passphrase) {
    return vault_create_and_publish(vault_dav_from_cfg(dav), passphrase);
}

VaultSetupResult vault_join(const std::vector<uint8_t>& server_keys, const char* secret,
                            bool recovery) {
    VaultSetupResult res = {nullptr, "", std::vector<uint8_t>(), ""};
    bool plain = false;
    if (!ffi_keys_mode(server_keys.data(), server_keys.size(), &plain)) {
        printf("[vault] join failed: ws_keys_mode: %s\n", ffi_err());
        res.error = kUnrecognizedKeys;
        return res;
    }
    if (!plain && (!secret || !secret[0])) {
        printf("[vault] join refused: encrypted server, no %s entered\n",
               recovery ? "recovery key" : "passphrase");
        res.error = vault_secret_required(recovery);
        return res;
    }
    const char* how = plain ? "no secret (plain)" : recovery ? "recovery key" : "passphrase";
    printf("[vault] join with %s (keys %zu bytes) ...\n", how, server_keys.size());
    res.vault = ws_vault_open(server_keys.data(), server_keys.size(), plain ? nullptr : secret,
                              recovery);
    if (!res.vault) {
        printf("[vault] join failed: %s\n", ffi_err());
        res.error = plain ? std::string("Could not open the server vault: ") + ffi_err()
                          : std::string(recovery ? "Wrong recovery key" : "Wrong passphrase");
        return res;
    }
    res.keys_json = server_keys;
    printf("[vault] join done\n");
    return res;
}

WsVault* vault_open_local(const uint8_t* keys, size_t len, const char* passphrase) {
    printf("[vault] opening local keys.json (%zu bytes) ...\n", len);
    WsVault* v = ws_vault_open(keys, len, passphrase, false);
    if (v) printf("[vault] local keys.json opened (encrypted=%d)\n", ws_vault_is_encrypted(v) ? 1 : 0);
    else   printf("[vault] local keys.json open failed: %s\n", ffi_err());
    return v;
}

bool vault_persist_local(const VaultSetupResult& r, const char* keys_path,
                         const std::string& recovery_path) {
    printf("[vault] persist local keys -> %s\n", keys_path);
    if (!write_whole_file(keys_path, r.keys_json.data(), r.keys_json.size())) return false;
    if (r.recovery_hex.empty()) return true;
    return write_whole_file(recovery_path, r.recovery_hex.data(), r.recovery_hex.size());
}

void vault_setup_persist(const VaultSetupResult& r, const char* keys_path,
                         const std::string& recovery_path, const WaystoneShellConfig& cfg,
                         const char* config_path) {
    if (!vault_persist_local(r, keys_path, recovery_path))
        printf("[vault] setup: local persist FAILED (server copy is intact)\n");
    bool cfg_ok = wsconfig_save(cfg, config_path);
    printf("[vault] setup: config.json save %s\n", cfg_ok ? "ok" : "FAILED");
}

void vault_setup_wipe_recovery(VaultSetupResult* r) {
    zeroize_string(r->recovery_hex);
}

void vault_setup_log_start(const char* what, bool plain) {
    printf("[vault] setup %s %s: start\n", what, keys_mode_name(plain));
}

void vault_setup_log_result(const char* what, bool plain, const VaultSetupResult& r) {
    if (r.vault) printf("[vault] setup %s %s: ok\n", what, keys_mode_name(plain));
    else         printf("[vault] setup %s %s: failed (%s)\n", what, keys_mode_name(plain), r.error.c_str());
}

void vault_publish_if_absent(const VaultDav& dav, const uint8_t* local_keys, size_t len) {
    RemoteKeys probe = vault_fetch_remote_keys(dav);
    switch (probe.state) {
    case RemoteKeysState::Absent: {
        int status = dav.put(dav.ctx, kKeysPath, local_keys, len);
        printf("[vault] publish local keys.json (%zu bytes) to %s%s: %s (http=%d)\n", len,
               dav.log_url.c_str(), kKeysPath, is_2xx(status) ? "ok" : "FAILED", status);
        break;
    }
    case RemoteKeysState::Present:
        if (probe.bytes.size() == len &&
            std::equal(probe.bytes.begin(), probe.bytes.end(), local_keys)) {
            printf("[vault] server /keys.json matches this console's\n");
        } else {
            printf("[vault] WARNING: server /keys.json is a different vault than this console's\n");
        }
        break;
    case RemoteKeysState::Error:
        printf("[vault] publish skipped: probe failed (http=%d)\n", probe.http_status);
        break;
    }
}

void vault_publish_if_absent(const WebDavCfg& dav, const uint8_t* local_keys, size_t len) {
    vault_publish_if_absent(vault_dav_from_cfg(dav), local_keys, len);
}

void vault_publish_local_if_absent(const VaultDav& dav, const char* keys_path) {
    long len = 0;
    uint8_t* keys = read_keys_file(keys_path, &len);
    if (!keys) {
        printf("[vault] publish check skipped: no local keys.json\n");
        return;
    }
    vault_publish_if_absent(dav, keys, static_cast<size_t>(len));
    free(keys);
}

void vault_publish_local_if_absent(const WebDavCfg& dav, const char* keys_path) {
    vault_publish_local_if_absent(vault_dav_from_cfg(dav), keys_path);
}
