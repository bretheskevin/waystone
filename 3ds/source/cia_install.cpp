#include "cia_install.h"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>

static const u32 CIA_CHUNK = 64 * 1024;

int cia_install_file(const char* path,
                     bool (*progress)(size_t done, size_t total, void* ctx),
                     void* ctx) {
    printf("[update] cia: install start path=%s\n", path ? path : "(null)");
    if (!path || !*path) return -1;

    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("[update] cia: open %s FAILED\n", path);
        return -1;
    }
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        printf("[update] cia: bad size %ld for %s\n", size, path);
        fclose(f);
        return -1;
    }
    u8* buf = static_cast<u8*>(malloc(CIA_CHUNK));
    if (!buf) {
        printf("[update] cia: chunk alloc FAILED (%lu B)\n", (unsigned long)CIA_CHUNK);
        fclose(f);
        return -1;
    }

    Result rc = amInit();
    if (R_FAILED(rc)) {
        printf("[update] cia: amInit FAILED rc=0x%08lX\n", (unsigned long)rc);
        free(buf);
        fclose(f);
        return -1;
    }
    printf("[update] cia: amInit ok, size=%ld B\n", size);

    Handle h = 0;
    rc = AM_StartCiaInstall(MEDIATYPE_SD, &h);
    if (R_FAILED(rc)) {
        printf("[update] cia: AM_StartCiaInstall FAILED rc=0x%08lX\n", (unsigned long)rc);
        amExit();
        free(buf);
        fclose(f);
        return -1;
    }
    printf("[update] cia: AM_StartCiaInstall ok (MEDIATYPE_SD)\n");

    const u64 total = (u64)size;
    u64 off = 0;
    bool ok = true;
    while (off < total) {
        if (progress && !progress((size_t)off, (size_t)total, ctx)) {
            printf("[update] cia: cancelled at %llu/%llu\n",
                   (unsigned long long)off, (unsigned long long)total);
            ok = false;
            break;
        }
        size_t n = fread(buf, 1, CIA_CHUNK, f);
        if (n == 0) {
            printf("[update] cia: read FAILED at %llu/%llu\n",
                   (unsigned long long)off, (unsigned long long)total);
            ok = false;
            break;
        }
        u32 written = 0;
        rc = FSFILE_Write(h, &written, off, buf, (u32)n, 0);
        if (R_FAILED(rc) || written != (u32)n) {
            printf("[update] cia: FSFILE_Write FAILED at %llu rc=0x%08lX written=%lu/%lu\n",
                   (unsigned long long)off, (unsigned long)rc,
                   (unsigned long)written, (unsigned long)n);
            ok = false;
            break;
        }
        off += n;
    }

    if (ok) {
        printf("[update] cia: streamed %llu B, finishing\n", (unsigned long long)off);
        rc = AM_FinishCiaInstall(h);  // consumes h even on failure -- never Cancel after this
        if (R_FAILED(rc)) {
            printf("[update] cia: AM_FinishCiaInstall FAILED rc=0x%08lX\n", (unsigned long)rc);
            ok = false;
        } else {
            printf("[update] cia: AM_FinishCiaInstall ok\n");
        }
    } else {
        Result crc = AM_CancelCIAInstall(h);
        printf("[update] cia: AM_CancelCIAInstall rc=0x%08lX\n", (unsigned long)crc);
    }

    amExit();
    free(buf);
    fclose(f);
    printf("[update] cia: amExit, install %s\n", ok ? "done" : "failed");
    return ok ? 0 : -1;
}
