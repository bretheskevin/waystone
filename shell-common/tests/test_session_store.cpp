// Host-only test for the auto-unlock session store (session.bin) on plain + encrypted servers.
// SESSION_STORE_PATH is the relative "sdmc:/waystone/session.bin" on the host, so the test runs
// from a temp dir that holds an "sdmc:" directory.
// Build (from repo root):
//   cargo build -p waystone-ffi
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_session_store.cpp shell-common/session_store.cpp \
//     target/debug/libwaystone_ffi.a -liconv -lSystem -lc -lm \
//     -o /tmp/test_session_store && /tmp/test_session_store
#include "session_store.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ffi_err.h"

namespace {

bool fake_device_key(uint8_t* out, size_t* len) {
    const uint8_t key[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    memcpy(out, key, sizeof key);
    *len = sizeof key;
    return true;
}

std::vector<uint8_t> keys_of(WsVault* v) {
    WsBuf b = ws_vault_keys_json(v);
    assert(b.ptr);
    std::vector<uint8_t> out(b.ptr, b.ptr + b.len);
    ws_buf_free(b);
    return out;
}

std::vector<uint8_t> mdk_of(WsVault* v) {
    WsBuf b = ws_vault_export_mdk(v);
    std::vector<uint8_t> out;
    if (b.ptr) out.assign(b.ptr, b.ptr + b.len);
    ws_buf_free(b);
    return out;
}

void test_plain_session_round_trip() {
    WsVault* v = ws_vault_plain();
    std::vector<uint8_t> keys = keys_of(v);
    assert(persist_session(v, "webdav-pw"));
    ws_vault_free(v);
    assert(session_store_exists());

    std::string pass;
    WsVault* loaded = session_store_load_vault(pass, keys.data(), keys.size());
    assert(loaded);
    assert(!ws_vault_is_encrypted(loaded));
    assert(pass == "webdav-pw");
    ws_vault_free(loaded);
    session_store_clear();
    printf("test_plain_session_round_trip PASSED\n");
}

void test_encrypted_session_round_trip() {
    WsBuf rec = {nullptr, 0};
    WsBuf kb = {nullptr, 0};
    WsVault* v = ws_vault_init("pw", &rec, &kb);
    assert(v);
    std::vector<uint8_t> keys(kb.ptr, kb.ptr + kb.len);
    ws_buf_free(rec);
    ws_buf_free(kb);
    std::vector<uint8_t> mdk = mdk_of(v);
    assert(mdk.size() == 32);
    assert(persist_session(v, "secret-pw"));
    ws_vault_free(v);

    std::string pass;
    WsVault* loaded = session_store_load_vault(pass, keys.data(), keys.size());
    assert(loaded && ws_vault_is_encrypted(loaded));
    assert(mdk_of(loaded) == mdk);
    assert(pass == "secret-pw");
    ws_vault_free(loaded);
    session_store_clear();
    printf("test_encrypted_session_round_trip PASSED\n");
}

void test_persist_null_vault_fails() {
    assert(!persist_session(nullptr, "pw"));
    assert(!session_store_exists());
    printf("test_persist_null_vault_fails PASSED\n");
}

void test_invalid_local_keys_fail() {
    WsVault* v = ws_vault_plain();
    assert(persist_session(v, "pw"));
    ws_vault_free(v);
    std::string pass;
    const char* junk = "junk";
    assert(!session_store_load_vault(pass, reinterpret_cast<const uint8_t*>(junk), 4));
    assert(pass.empty());
    assert(!session_store_load_vault(pass, nullptr, 0));
    assert(pass.empty());
    session_store_clear();
    printf("test_invalid_local_keys_fail PASSED\n");
}

void test_update_webdav_pass_on_plain_vault() {
    WsVault* v = ws_vault_plain();
    std::vector<uint8_t> keys = keys_of(v);
    std::string current = "old";
    std::string entered = "new-pw";
    assert(update_webdav_pass(v, current, entered));
    ws_vault_free(v);
    std::string pass;
    WsVault* loaded = session_store_load_vault(pass, keys.data(), keys.size());
    assert(loaded && pass == "new-pw");
    ws_vault_free(loaded);
    session_store_clear();
    printf("test_update_webdav_pass_on_plain_vault PASSED\n");
}

void test_ffi_keys_plain_and_mode_name() {
    WsVault* v = ws_vault_plain();
    std::vector<uint8_t> plain = keys_of(v);
    ws_vault_free(v);
    WsBuf rec = {nullptr, 0};
    WsBuf kb = {nullptr, 0};
    v = ws_vault_init("pw", &rec, &kb);
    assert(v);
    std::vector<uint8_t> enc(kb.ptr, kb.ptr + kb.len);
    ws_buf_free(rec);
    ws_buf_free(kb);
    ws_vault_free(v);

    assert(ffi_keys_plain(plain.data(), plain.size()));
    assert(!ffi_keys_plain(enc.data(), enc.size()));
    assert(!ffi_keys_plain(reinterpret_cast<const uint8_t*>("junk"), 4));
    assert(!ffi_keys_plain(nullptr, 0));
    assert(strcmp(keys_mode_name(true), "plain") == 0);
    assert(strcmp(keys_mode_name(false), "encrypted") == 0);
    printf("test_ffi_keys_plain_and_mode_name PASSED\n");
}

} // namespace

int main() {
    char tmpl[] = "/private/tmp/ws_session_store_XXXXXX";
    assert(mkdtemp(tmpl));
    assert(chdir(tmpl) == 0);
    assert(mkdir("sdmc:", 0755) == 0);
    session_store_set_device_key_fn(fake_device_key);

    test_plain_session_round_trip();
    test_encrypted_session_round_trip();
    test_persist_null_vault_fails();
    test_invalid_local_keys_fail();
    test_update_webdav_pass_on_plain_vault();
    test_ffi_keys_plain_and_mode_name();

    std::string cmd = std::string("rm -rf '") + tmpl + "'";
    assert(system(cmd.c_str()) == 0);
    printf("ALL session_store TESTS PASSED\n");
    return 0;
}
