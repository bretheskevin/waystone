#include "session_store.h"
#include "secure_clear.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

extern "C" {
struct Vault;
#include "waystone.h"
}

static DeviceKeyFn g_device_key_fn = nullptr;

void session_store_set_device_key_fn(DeviceKeyFn fn) { g_device_key_fn = fn; }

static const char MAGIC[4] = {'W', 'S', '0', '1'};

// Derive a 32-byte obfuscation key from the hardware device key.
// SHA-256("waystone-session-v1" || device_key) via ws_content_hash FFI.
static bool derive_obfuscation_key(uint8_t key_out[32]) {
    if (!g_device_key_fn) return false;

    uint8_t device_key[64];
    size_t device_key_len = sizeof(device_key);
    if (!g_device_key_fn(device_key, &device_key_len)) {
        secure_clear(device_key, sizeof(device_key));
        return false;
    }

    const char* domain = "waystone-session-v1";
    size_t domain_len = strlen(domain);
    size_t input_len = domain_len + device_key_len;
    uint8_t* input = new uint8_t[input_len];
    memcpy(input, domain, domain_len);
    memcpy(input + domain_len, device_key, device_key_len);
    secure_clear(device_key, sizeof(device_key));

    char* hash_hex = ws_content_hash(input, input_len);
    secure_clear(input, input_len);
    delete[] input;

    if (!hash_hex) return false;

    for (int i = 0; i < 32; i++) {
        unsigned hi, lo;
        sscanf(hash_hex + i * 2, "%1x", &hi);
        sscanf(hash_hex + i * 2 + 1, "%1x", &lo);
        key_out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    ws_string_free(hash_hex);
    return true;
}

bool session_store_save(const uint8_t* mdk, size_t mdk_len,
                        const std::string& webdav_pass) {
    if (mdk_len != 32) return false;
    if (webdav_pass.size() > 0xFFFF) return false;

    uint8_t obf_key[32];
    if (!derive_obfuscation_key(obf_key)) return false;

    uint16_t pass_len = static_cast<uint16_t>(webdav_pass.size());
    size_t payload_len = 32 + 2 + pass_len;
    uint8_t* payload = new uint8_t[payload_len];
    memcpy(payload, mdk, 32);
    payload[32] = static_cast<uint8_t>(pass_len & 0xFF);
    payload[33] = static_cast<uint8_t>((pass_len >> 8) & 0xFF);
    memcpy(payload + 34, webdav_pass.data(), pass_len);

    WsVault* tmp_vault = ws_vault_from_mdk(obf_key, 32);
    secure_clear(obf_key, 32);
    if (!tmp_vault) {
        secure_clear(payload, payload_len);
        delete[] payload;
        return false;
    }

    WsBuf encrypted = ws_vault_encrypt_blob(tmp_vault, payload, payload_len);
    secure_clear(payload, payload_len);
    delete[] payload;
    ws_vault_free(tmp_vault);

    if (!encrypted.ptr || encrypted.len == 0) {
        ws_buf_free(encrypted);
        return false;
    }

    mkdir("sdmc:/waystone", 0755);
    FILE* f = fopen(SESSION_STORE_PATH, "wb");
    if (!f) {
        ws_buf_free(encrypted);
        return false;
    }

    uint32_t enc_len = static_cast<uint32_t>(encrypted.len);
    uint8_t len_bytes[4] = {
        static_cast<uint8_t>(enc_len & 0xFF),
        static_cast<uint8_t>((enc_len >> 8) & 0xFF),
        static_cast<uint8_t>((enc_len >> 16) & 0xFF),
        static_cast<uint8_t>((enc_len >> 24) & 0xFF),
    };

    bool ok = (fwrite(MAGIC, 1, 4, f) == 4)
           && (fwrite(len_bytes, 1, 4, f) == 4)
           && (fwrite(encrypted.ptr, 1, encrypted.len, f) == encrypted.len);

    fclose(f);
    ws_buf_free(encrypted);
    return ok;
}

bool session_store_load(uint8_t* mdk_out, size_t mdk_cap,
                        std::string& pass_out) {
    pass_out.clear();
    if (mdk_cap < 32) return false;
    memset(mdk_out, 0, mdk_cap);

    FILE* f = fopen(SESSION_STORE_PATH, "rb");
    if (!f) return false;

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }

    uint8_t len_bytes[4];
    if (fread(len_bytes, 1, 4, f) != 4) {
        fclose(f); return false;
    }
    uint32_t enc_len = static_cast<uint32_t>(len_bytes[0])
                     | (static_cast<uint32_t>(len_bytes[1]) << 8)
                     | (static_cast<uint32_t>(len_bytes[2]) << 16)
                     | (static_cast<uint32_t>(len_bytes[3]) << 24);

    if (enc_len == 0 || enc_len > 1024 * 1024) {
        fclose(f); return false;
    }

    uint8_t* enc_data = new uint8_t[enc_len];
    if (fread(enc_data, 1, enc_len, f) != enc_len) {
        delete[] enc_data;
        fclose(f);
        return false;
    }
    fclose(f);

    uint8_t obf_key[32];
    if (!derive_obfuscation_key(obf_key)) {
        delete[] enc_data;
        return false;
    }

    WsVault* tmp_vault = ws_vault_from_mdk(obf_key, 32);
    secure_clear(obf_key, 32);
    if (!tmp_vault) {
        delete[] enc_data;
        return false;
    }

    WsBuf decrypted = ws_vault_decrypt_blob(tmp_vault, enc_data, enc_len);
    delete[] enc_data;
    ws_vault_free(tmp_vault);

    if (!decrypted.ptr || decrypted.len < 34) {
        if (decrypted.ptr) secure_clear(decrypted.ptr, decrypted.len);
        ws_buf_free(decrypted);
        return false;
    }

    memcpy(mdk_out, decrypted.ptr, 32);
    uint16_t pass_len = static_cast<uint16_t>(decrypted.ptr[32])
                      | (static_cast<uint16_t>(decrypted.ptr[33]) << 8);

    if (static_cast<size_t>(34) + pass_len > decrypted.len) {
        secure_clear(mdk_out, mdk_cap);
        secure_clear(decrypted.ptr, decrypted.len);
        ws_buf_free(decrypted);
        return false;
    }

    pass_out.assign(reinterpret_cast<const char*>(decrypted.ptr + 34), pass_len);

    secure_clear(decrypted.ptr, decrypted.len);
    ws_buf_free(decrypted);
    return true;
}

bool session_store_exists() {
    FILE* f = fopen(SESSION_STORE_PATH, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    return sz > 8;
}

void session_store_clear() {
    FILE* f = fopen(SESSION_STORE_PATH, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        if (sz > 0) {
            f = fopen(SESSION_STORE_PATH, "wb");
            if (f) {
                uint8_t zeros[256];
                memset(zeros, 0, sizeof(zeros));
                long written = 0;
                while (written < sz) {
                    size_t chunk = (sz - written > 256) ? 256 : static_cast<size_t>(sz - written);
                    fwrite(zeros, 1, chunk, f);
                    written += static_cast<long>(chunk);
                }
                fclose(f);
            }
        }
    }
    remove(SESSION_STORE_PATH);
}

void persist_session(WsVault* vault, const std::string& webdav_pass) {
    WsBuf mdk_buf = ws_vault_export_mdk(vault);
    if (mdk_buf.ptr && mdk_buf.len == 32) {
        session_store_save(mdk_buf.ptr, mdk_buf.len, webdav_pass);
    }
    if (mdk_buf.ptr) secure_clear(mdk_buf.ptr, mdk_buf.len);
    ws_buf_free(mdk_buf);
}

WsVault* session_store_load_vault(std::string& pass_out) {
    uint8_t mdk[32];
    size_t cap = sizeof(mdk);
    if (!session_store_load(mdk, cap, pass_out)) { secure_clear(mdk, 32); return nullptr; }
    WsVault* v = ws_vault_from_mdk(mdk, 32);
    secure_clear(mdk, 32);
    if (!v && !pass_out.empty()) { secure_clear(&pass_out[0], pass_out.size()); pass_out.clear(); }
    return v;
}
