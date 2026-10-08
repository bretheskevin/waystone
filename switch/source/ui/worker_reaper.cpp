#include "worker_reaper.h"
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
