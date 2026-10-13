// Host-only test for log_cap_rotate: size cap, rotation, rename-failure fallback.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra \
//     -I shell-common \
//     shell-common/tests/test_log_rotate.cpp shell-common/log_rotate.cpp \
//     -o /tmp/test_log_rotate && /tmp/test_log_rotate
#include "log_rotate.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

static long file_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (long)st.st_size;
}

static void write_file(const char* path, long bytes) {
    FILE* f = fopen(path, "w");
    assert(f);
    for (long i = 0; i < bytes; i++) fputc('x', f);
    fclose(f);
}

int main() {
    const char* dir = "/tmp/waystone_test_log_rotate";
    char path[256], old_path[264];
    snprintf(path, sizeof(path), "%s/log.txt", dir);
    snprintf(old_path, sizeof(old_path), "%s/log.txt.old", dir);
    char mkdir_cmd[300];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", dir);
    assert(system(mkdir_cmd) == 0);

    // Clean slate (leftovers from a previous run would skew the assertions).
    remove(path);
    remove(old_path);

    // Missing file: no-op.
    log_cap_rotate(path, 100);
    assert(file_size(path) == -1);

    // Under the cap: untouched.
    write_file(path, 50);
    log_cap_rotate(path, 100);
    assert(file_size(path) == 50);
    assert(file_size(old_path) == -1);

    // Over the cap: rotated to .old, live path gone.
    write_file(path, 150);
    log_cap_rotate(path, 100);
    assert(file_size(path) == -1);
    assert(file_size(old_path) == 150);

    // Second rotation overwrites the previous .old.
    write_file(path, 120);
    log_cap_rotate(path, 100);
    assert(file_size(path) == -1);
    assert(file_size(old_path) == 120);

    // Exactly at the cap: untouched.
    write_file(path, 100);
    log_cap_rotate(path, 100);
    assert(file_size(path) == 100);

    // Defensive: null path / non-positive cap.
    log_cap_rotate(NULL, 100);
    write_file(path, 200);
    log_cap_rotate(path, 0);
    assert(file_size(path) == 200);

    printf("test_log_rotate PASSED\n");
    return 0;
}
