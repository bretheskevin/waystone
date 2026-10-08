#pragma once
#include <cstddef>

// Installs the CIA at `path` onto the SD card through AM (amInit/amExit inside).
// Streams 64 KiB chunks into the AM import handle; progress(done, total, ctx) runs on
// the calling thread before each chunk, and returning false cancels the import. Any
// failure leaves the installed title untouched. Returns 0 on success, -1 on failure.
// Never removes `path` -- the caller owns the temp file.
int cia_install_file(const char* path,
                     bool (*progress)(size_t done, size_t total, void* ctx),
                     void* ctx);
