#include "log_rotate.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>

void log_cap_rotate(const char* path, long max_bytes) {
    if (!path || max_bytes <= 0) return;
    struct stat st;
    if (stat(path, &st) != 0) return;
    if (st.st_size <= max_bytes) return;

    std::string old_path = std::string(path) + ".old";
    remove(old_path.c_str());
    if (rename(path, old_path.c_str()) != 0) {
        // FS refused the rename: drop the oversized log rather than grow unbounded.
        remove(path);
    }
}
