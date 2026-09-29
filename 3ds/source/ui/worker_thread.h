#pragma once
#include <3ds.h>

// Returns true when running on a New 3DS. Lazy-initialised; requires APT (call after gfxInitDefault).
// NOTE: declared inline (not static inline) so the static `cached` is shared across all TUs.
inline bool is_new_3ds(void) {
    static int cached = -1;
    if (cached < 0) {
        bool out = false;
        cached = (R_SUCCEEDED(APT_CheckNew3DS(&out)) && out) ? 1 : 0;
    }
    return cached == 1;
}

// Shared 3DS worker-thread launch: one priority below the caller (render) thread.
// On New 3DS, targets core2 (the extra app-usable core) for true parallelism with the
// render thread. Falls back to the default app core (-2) if core2 is not accessible
// (e.g. HBL did not grant the 0x2000 exheader kernel flag).
static inline Thread start_worker_thread(ThreadFunc entry, void* ctx) {
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    int core = is_new_3ds() ? 2 : -2;
    Thread t = threadCreate(entry, ctx, 128 * 1024, prio + 1, core, false);
    if (t && core == 2) {
        printf("[sys] worker thread on core2\n");
    } else if (!t && core == 2) {
        printf("[sys] core2 threadCreate failed, falling back to core0\n");
        t = threadCreate(entry, ctx, 128 * 1024, prio + 1, -2, false);
        if (t)  printf("[sys] worker thread on core0 (fallback ok)\n");
    }
    if (!t) printf("[sys] worker threadCreate failed\n");
    return t;
}
