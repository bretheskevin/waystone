#pragma once
#include <cstddef>
#include <functional>
#include "busy_policy.h"

struct Session;

// Deletes workers whose thread may still be running when their activity is popped, polled on the
// borealis main thread (RepeatingTask, 100 ms). Never joins on the render thread.
// worker_reaper_start() must run once at boot (starting a Ticking from inside an animation
// callback -- e.g. an activity destructor -- would mutate the running tickings list).
void worker_reaper_start();
void defer_reap(std::function<bool()> try_reap);  // try_reap returns true once it deleted its worker
size_t worker_reaper_pending();

// True (logged under `tag` + toasted) when `what` must wait for a running sync, the conflict worker
// (unless scope is Resolve) or reaped workers that may still write saves / borrow the session.
// Decision: busy_refusal(); session may be null (no dashboard hooks = nothing running).
bool refuse_if_busy(BusyScope scope, const Session* session, const char* tag, const char* what);

template <typename W>
void reap_worker(W*& worker) {
    if (!worker) return;
    W* w = worker;
    worker = nullptr;
    w->request_cancel();
    defer_reap([w]() {
        if (w->is_running()) return false;
        delete w;
        return true;
    });
}
