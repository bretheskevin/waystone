#include "worker_reaper.h"
#include "kit/toast.h"
#include "session.h"
#include <borealis.hpp>
#include <cstdio>
#include <vector>

namespace {
std::vector<std::function<bool()>>& reapers() {
    static std::vector<std::function<bool()>> v;
    return v;
}
class ReaperTask : public brls::RepeatingTask {
  public:
    ReaperTask() : brls::RepeatingTask(100) {}
    void run() override {
        auto& v = reapers();
        for (size_t i = 0; i < v.size();) {
            auto fn = v[i];
            if (fn()) {
                v.erase(v.begin() + (long)i);
                printf("[ui] reaper: worker deleted (%zu pending)\n", v.size());
            } else {
                ++i;
            }
        }
    }
};
ReaperTask* g_task = nullptr;
}

void worker_reaper_start() {
    if (g_task) return;
    g_task = new ReaperTask();
    g_task->start();
    printf("[ui] reaper: started\n");
}

void defer_reap(std::function<bool()> try_reap) {
    if (try_reap()) {
        printf("[ui] reaper: worker already idle, deleted now\n");
        return;
    }
    reapers().push_back(std::move(try_reap));
    printf("[ui] reaper: deferred (%zu pending)\n", reapers().size());
    if (!g_task) {
        printf("[ui] reaper: WARNING not started at boot -- starting now\n");
        worker_reaper_start();
    }
}

size_t worker_reaper_pending() { return reapers().size(); }

bool refuse_if_busy(BusyScope scope, const Session* session, const char* tag, const char* what) {
    const bool sync    = session && session->sync_running && session->sync_running();
    const bool busy    = session && session->sync_busy && session->sync_busy();
    const size_t reaping = worker_reaper_pending();
    const char* why = busy_refusal(scope, sync, busy, reaping);
    if (!why) return false;
    printf("%s %s refused: %s (sync=%d busy=%d reaping=%zu)\n", tag, what, why, (int)sync, (int)busy, reaping);
    ws_toast(sync ? "Sync in progress \xe2\x80\x94 wait for it to finish"
                  : "Finishing a background task \xe2\x80\x94 try again in a moment");
    return true;
}
