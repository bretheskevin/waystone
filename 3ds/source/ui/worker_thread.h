#pragma once
#include <3ds.h>

// Shared 3DS worker-thread launch: app-core, one priority below the caller (render) thread.
static inline Thread start_worker_thread(ThreadFunc entry, void* ctx) {
    s32 prio = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    return threadCreate(entry, ctx, 128 * 1024, prio + 1, -2, false);
}
