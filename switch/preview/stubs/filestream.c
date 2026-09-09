/*
 * Minimal stubs for libretro-common filestream functions used by features_cpu.c.
 * features_cpu.c reads /proc/cpuinfo to detect CPU features; returning NULL
 * causes graceful failure (no feature flags set) which is fine for our preview build.
 */
#include <stddef.h>

typedef void RFILE;

RFILE* filestream_open(const char* path, unsigned mode, unsigned hints)
{
    (void)path; (void)mode; (void)hints;
    return NULL;
}

char* filestream_gets(RFILE* stream, char* buf, size_t len)
{
    (void)stream; (void)buf; (void)len;
    return NULL;
}

int filestream_close(RFILE* stream)
{
    (void)stream;
    return 0;
}
