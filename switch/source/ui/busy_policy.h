#pragma once
#include <cstddef>

// What a refused action would have written: Restore (save restore), Resolve (conflict keep
// local/remote — designed to queue on the pane's own scan), Credentials (WebDAV edits / logout).
enum class BusyScope { Restore, Resolve, Credentials };

static const char* const WS_BUSY_SYNC     = "a sync is running";
static const char* const WS_BUSY_CONFLICT = "a conflict scan is running";
static const char* const WS_BUSY_REAPING  = "background worker(s) still finishing";

// nullptr when the action may run, else the (loggable) reason it must wait.
inline const char* busy_refusal(BusyScope scope, bool sync_running, bool conflict_busy, size_t reaping) {
    if (sync_running) return WS_BUSY_SYNC;
    if (conflict_busy && scope != BusyScope::Resolve) return WS_BUSY_CONFLICT;
    if (reaping > 0) return WS_BUSY_REAPING;
    return nullptr;
}
