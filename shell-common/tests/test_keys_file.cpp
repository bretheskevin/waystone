// Host-only test for read_keys_file.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_keys_file.cpp shell-common/keys_file.cpp \
//     -o /tmp/test_keys_file && /tmp/test_keys_file
#include "keys_file.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
    const char* path = "/tmp/waystone_test_keys.json";
    FILE* f = fopen(path, "wb");
    assert(f);
    fputs("{\"k\":1}", f);
    fclose(f);
    long len = -1;
    uint8_t* buf = read_keys_file(path, &len);
    assert(buf && len == 7 && memcmp(buf, "{\"k\":1}", 7) == 0);
    free(buf);
    f = fopen(path, "wb"); fclose(f);              // empty file
    assert(read_keys_file(path, &len) == nullptr && len == 0);
    remove(path);
    assert(read_keys_file(path, &len) == nullptr && len == 0);   // missing file
    printf("ALL PASSED\n");
    return 0;
}
