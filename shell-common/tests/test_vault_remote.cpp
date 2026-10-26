// Host-only test for the server-side vault create/join logic over an in-memory WebDAV.
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common -I ffi/include \
//     shell-common/tests/test_vault_remote.cpp shell-common/vault_remote.cpp \
//     shell-common/net.cpp shell-common/snapshot.cpp shell-common/file_tree.cpp \
//     shell-common/keys_file.cpp shell-common/wsconfig.cpp shell-common/json.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm -lcurl \
//     -o /tmp/test_vault_remote && /tmp/test_vault_remote
#include "vault_remote.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>

namespace {

struct FakeServer {
    std::map<std::string, std::vector<uint8_t> > files;
    int get_status_override = 0;
    int put_status_override = 0;
    int gets = 0;
    int puts = 0;
    std::vector<uint8_t> last_put;
};

int fake_get(void* ctx, const char* path, std::vector<uint8_t>* out) {
    FakeServer* s = static_cast<FakeServer*>(ctx);
    s->gets++;
    if (s->get_status_override) return s->get_status_override;
    std::map<std::string, std::vector<uint8_t> >::const_iterator it = s->files.find(path);
    if (it == s->files.end()) return 404;
    *out = it->second;
    return 200;
}

int fake_put(void* ctx, const char* path, const uint8_t* data, size_t len) {
    FakeServer* s = static_cast<FakeServer*>(ctx);
    s->puts++;
    if (s->put_status_override) return s->put_status_override;
    s->last_put.assign(data, data + len);
    s->files[path] = s->last_put;
    return 201;
}

VaultDav fake_dav(FakeServer* s) {
    VaultDav d;
    d.get = fake_get;
    d.put = fake_put;
    d.ctx = s;
    d.log_url = "http://fake";
    return d;
}

std::vector<uint8_t> bytes_of(const char* s) {
    return std::vector<uint8_t>(s, s + strlen(s));
}

struct RealVault {
    std::vector<uint8_t> keys;
    std::string recovery;
};

RealVault make_real_vault(const char* pass) {
    WsBuf rec = {nullptr, 0};
    WsBuf keys = {nullptr, 0};
    WsVault* v = ws_vault_init(pass, &rec, &keys);
    assert(v);
    RealVault rv;
    rv.keys.assign(keys.ptr, keys.ptr + keys.len);
    rv.recovery.assign(reinterpret_cast<const char*>(rec.ptr), rec.len);
    ws_buf_free(rec);
    ws_buf_free(keys);
    ws_vault_free(v);
    return rv;
}

std::string read_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

bool file_exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

void test_fetch_states() {
    FakeServer s;
    VaultDav d = fake_dav(&s);
    RemoteKeys r = vault_fetch_remote_keys(d);
    assert(r.state == RemoteKeysState::Absent && r.http_status == 404);

    s.files["/keys.json"] = bytes_of("{}");
    r = vault_fetch_remote_keys(d);
    assert(r.state == RemoteKeysState::Present && r.bytes == bytes_of("{}"));

    s.files["/keys.json"].clear();
    r = vault_fetch_remote_keys(d);
    assert(r.state == RemoteKeysState::Error);

    s.get_status_override = 500;
    r = vault_fetch_remote_keys(d);
    assert(r.state == RemoteKeysState::Error && r.http_status == 500);
    printf("test_fetch_states PASSED\n");
}

void test_create_on_absent_publishes_keys() {
    FakeServer s;
    VaultSetupResult r = vault_create_and_publish(fake_dav(&s), "correct horse");
    assert(r.error.empty());
    assert(r.vault);
    assert(!r.recovery_hex.empty());
    assert(!r.keys_json.empty());
    assert(s.puts == 1);
    assert(s.last_put == r.keys_json);
    assert(s.files["/keys.json"] == r.keys_json);
    ws_vault_free(r.vault);
    vault_setup_wipe_recovery(&r);
    assert(r.recovery_hex.empty());
    printf("test_create_on_absent_publishes_keys PASSED\n");
}

void test_create_refuses_when_present() {
    FakeServer s;
    s.files["/keys.json"] = bytes_of("{\"v\":1}");
    VaultSetupResult r = vault_create_and_publish(fake_dav(&s), "pw");
    assert(!r.vault);
    assert(r.error == "A vault already exists on this server - join it instead");
    assert(s.puts == 0);
    printf("test_create_refuses_when_present PASSED\n");
}

void test_create_refuses_on_get_error() {
    FakeServer s;
    s.get_status_override = 503;
    VaultSetupResult r = vault_create_and_publish(fake_dav(&s), "pw");
    assert(!r.vault);
    assert(r.error.find("503") != std::string::npos);
    assert(s.puts == 0);
    printf("test_create_refuses_on_get_error PASSED\n");
}

void test_create_put_failure_returns_nothing() {
    FakeServer s;
    s.put_status_override = 507;
    VaultSetupResult r = vault_create_and_publish(fake_dav(&s), "pw");
    assert(!r.vault);
    assert(r.recovery_hex.empty());
    assert(r.keys_json.empty());
    assert(r.error.find("507") != std::string::npos);
    assert(s.puts == 1);
    printf("test_create_put_failure_returns_nothing PASSED\n");
}

void test_create_put_conflict_means_vault_exists() {
    FakeServer s;
    s.put_status_override = 412;
    VaultSetupResult r = vault_create_and_publish(fake_dav(&s), "pw");
    assert(!r.vault);
    assert(r.recovery_hex.empty() && r.keys_json.empty());
    assert(r.error == "A vault already exists on this server - join it instead");
    printf("test_create_put_conflict_means_vault_exists PASSED\n");
}

void test_join_with_passphrase() {
    RealVault rv = make_real_vault("join me");
    VaultSetupResult r = vault_join(rv.keys, "join me", false);
    assert(r.error.empty());
    assert(r.vault);
    assert(r.keys_json == rv.keys);
    assert(r.recovery_hex.empty());
    ws_vault_free(r.vault);

    VaultSetupResult bad = vault_join(rv.keys, "nope", false);
    assert(!bad.vault);
    assert(bad.error == "Wrong passphrase");
    assert(bad.keys_json.empty());

    VaultSetupResult rec = vault_join(rv.keys, rv.recovery, true);
    assert(rec.error.empty() && rec.vault && rec.keys_json == rv.keys);
    ws_vault_free(rec.vault);

    VaultSetupResult badrec = vault_join(rv.keys, std::string(64, '0'), true);
    assert(!badrec.vault && badrec.error == "Wrong recovery key");
    printf("test_join_with_passphrase PASSED\n");
}

void test_publish_if_absent() {
    std::vector<uint8_t> local = bytes_of("{\"local\":1}");

    FakeServer absent;
    vault_publish_if_absent(fake_dav(&absent), local.data(), local.size());
    assert(absent.puts == 1);
    assert(absent.files["/keys.json"] == local);

    FakeServer same;
    same.files["/keys.json"] = local;
    vault_publish_if_absent(fake_dav(&same), local.data(), local.size());
    assert(same.puts == 0);

    FakeServer other;
    other.files["/keys.json"] = bytes_of("{\"other\":1}");
    vault_publish_if_absent(fake_dav(&other), local.data(), local.size());
    assert(other.puts == 0);
    assert(other.files["/keys.json"] == bytes_of("{\"other\":1}"));

    FakeServer err;
    err.get_status_override = 500;
    vault_publish_if_absent(fake_dav(&err), local.data(), local.size());
    assert(err.puts == 0);
    printf("test_publish_if_absent PASSED\n");
}

void test_persist_local() {
    char tmpl[] = "/private/tmp/ws_vault_remote_XXXXXX";
    assert(mkdtemp(tmpl));
    std::string root(tmpl);

    VaultSetupResult created = {nullptr, "abcd1234", bytes_of("{\"k\":1}"), ""};
    std::string keys_path = root + "/a/waystone/keys.json";
    std::string rec_path = root + "/a/waystone/recovery-dev.txt";
    assert(vault_persist_local(created, keys_path.c_str(), rec_path));
    assert(read_file(keys_path) == "{\"k\":1}");
    assert(read_file(rec_path) == "abcd1234");

    VaultSetupResult joined = {nullptr, "", bytes_of("{\"k\":2}"), ""};
    std::string keys2 = root + "/b/keys.json";
    std::string rec2 = root + "/b/recovery-dev.txt";
    assert(vault_persist_local(joined, keys2.c_str(), rec2));
    assert(read_file(keys2) == "{\"k\":2}");
    assert(!file_exists(rec2));

    std::string cmd = "rm -rf '" + root + "'";
    assert(system(cmd.c_str()) == 0);
    printf("test_persist_local PASSED\n");
}

void test_setup_persist_writes_keys_and_config() {
    char tmpl[] = "/private/tmp/ws_vault_setup_XXXXXX";
    assert(mkdtemp(tmpl));
    std::string root(tmpl);
    WaystoneShellConfig cfg;
    cfg.server_url = "https://dav.example.com";
    cfg.username   = "me";

    VaultSetupResult created = {nullptr, "beef", bytes_of("{\"k\":3}"), ""};
    std::string keys_path = root + "/w/keys.json";
    std::string cfg_path  = root + "/w/config.json";
    vault_setup_persist(created, keys_path.c_str(), root + "/w/recovery-dev.txt", cfg,
                        cfg_path.c_str());
    assert(read_file(keys_path) == "{\"k\":3}");
    assert(read_file(root + "/w/recovery-dev.txt") == "beef");
    assert(wsconfig_load(cfg_path.c_str()).server_url == "https://dav.example.com");

    std::string cmd = "rm -rf '" + root + "'";
    assert(system(cmd.c_str()) == 0);
    printf("test_setup_persist_writes_keys_and_config PASSED\n");
}

void test_publish_local_if_absent() {
    char tmpl[] = "/private/tmp/ws_vault_local_XXXXXX";
    assert(mkdtemp(tmpl));
    std::string keys_path = std::string(tmpl) + "/keys.json";

    FakeServer none;
    vault_publish_local_if_absent(fake_dav(&none), keys_path.c_str());
    assert(none.gets == 0 && none.puts == 0);

    VaultSetupResult local = {nullptr, "", bytes_of("{\"local\":2}"), ""};
    assert(vault_persist_local(local, keys_path.c_str(), ""));
    FakeServer absent;
    vault_publish_local_if_absent(fake_dav(&absent), keys_path.c_str());
    assert(absent.puts == 1 && absent.files["/keys.json"] == local.keys_json);

    std::string cmd = std::string("rm -rf '") + tmpl + "'";
    assert(system(cmd.c_str()) == 0);
    printf("test_publish_local_if_absent PASSED\n");
}

} // namespace

int main() {
    test_fetch_states();
    test_create_on_absent_publishes_keys();
    test_create_refuses_when_present();
    test_create_refuses_on_get_error();
    test_create_put_failure_returns_nothing();
    test_create_put_conflict_means_vault_exists();
    test_join_with_passphrase();
    test_publish_if_absent();
    test_persist_local();
    test_setup_persist_writes_keys_and_config();
    test_publish_local_if_absent();
    printf("ALL vault_remote TESTS PASSED\n");
    return 0;
}
