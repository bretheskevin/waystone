#include <cstdio>
#include <switch.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

int main(int argc, char* argv[]) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("waystone libnx link spike\n\n");

    WsBuf recovery = {nullptr, 0};
    WsBuf keys     = {nullptr, 0};
    WsBuf zip      = {nullptr, 0};
    WsBuf enc      = {nullptr, 0};
    WsBuf dec      = {nullptr, 0};
    char* hash     = nullptr;
    char* files    = nullptr;
    WsVault* v     = nullptr;

    v = ws_vault_init("spike-pass", &recovery, &keys);
    if (!v) {
        printf("FAIL ws_vault_init: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        goto cleanup;
    }

    zip = ws_canonical_zip(
        "[{\"path\":\"s.dat\",\"data_b64\":\"AAECAw==\"}]");
    if (!zip.ptr) {
        printf("FAIL ws_canonical_zip: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        goto cleanup;
    }

    hash = ws_content_hash(zip.ptr, zip.len);

    enc = ws_vault_encrypt_blob(v, zip.ptr, zip.len);
    if (!enc.ptr) {
        printf("FAIL ws_vault_encrypt_blob: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        goto cleanup;
    }

    dec = ws_vault_decrypt_blob(v, enc.ptr, enc.len);
    if (!dec.ptr) {
        printf("FAIL ws_vault_decrypt_blob: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        goto cleanup;
    }

    files = ws_unzip(dec.ptr, dec.len);

    {
        const char* err = ws_last_error();
        printf("hash=%s\nround-trip ok=%d\nfiles=%s\nlast_error=%s\n",
               hash ? hash : "(null)",
               (int)(dec.len == zip.len),
               files ? files : "(null)",
               err ? err : "none");
    }

cleanup:
    ws_string_free(files);
    ws_buf_free(dec);
    ws_buf_free(enc);
    ws_string_free(hash);
    ws_buf_free(zip);
    ws_buf_free(recovery);
    ws_buf_free(keys);
    if (v) ws_vault_free(v);

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }

    consoleExit(NULL);
    return 0;
}
