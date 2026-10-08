#include "keys_file.h"
#include <cstdio>
#include <cstdlib>

uint8_t* read_keys_file(const char* path, long* len_out) {
    *len_out = 0;
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[vault] keys file %s not found\n", path); return nullptr; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); printf("[vault] keys file %s empty\n", path); return nullptr; }
    uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(len)));
    if (!buf) { fclose(f); printf("[vault] keys file alloc %ld failed\n", len); return nullptr; }
    size_t got = fread(buf, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) {
        free(buf);
        printf("[vault] keys file %s short read (%zu/%ld)\n", path, got, len);
        return nullptr;
    }
    *len_out = len;
    printf("[vault] keys file %s read (%ld bytes)\n", path, len);
    return buf;
}
