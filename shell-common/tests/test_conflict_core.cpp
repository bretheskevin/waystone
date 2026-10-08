// Host-only test for the shared conflict scan/queue/resolve core (conflict_core.h).
// Engine entry points and the WebDAV session pair are faked here; the engine is covered by
// test_sync_engine.cpp. Enqueue/cancel "during" a scan run from inside the fake sync_scan_title.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_conflict_core.cpp -o /tmp/test_conflict_core && /tmp/test_conflict_core
#include "conflict_core.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

struct FakeTitle {
    uint64_t title_id;
    std::string name;
    bool has_remote;
    FakeTitle() : title_id(0), has_remote(true) {}
};
typedef ConflictCore<FakeTitle> Core;

static int g_depth = 0;
static int g_lock_calls = 0;
static std::vector<std::string> g_events;
static std::map<std::string, std::vector<SaveDecision> > g_scan;
static int g_push_rc = 0;
static int g_pull_rc = 0;
static bool g_session_fail = false;
static int g_prepared = 0;
static Core* g_core = 0;
static void (*g_on_scan)(const std::string& name) = 0;
static char g_sess_token;
static const char* kBusy = "Busy \xe2\x80\x94 try again in a moment";

static void reset() {
    assert(g_depth == 0);
    g_events.clear(); g_scan.clear();
    g_push_rc = 0; g_pull_rc = 0; g_session_fail = false;
    g_prepared = 0; g_on_scan = 0; g_lock_calls = 0;
}

static void t_lock(void*) { assert(g_depth == 0); g_depth++; g_lock_calls++; }  // non-recursive
static void t_unlock(void*) { assert(g_depth == 1); g_depth--; }

SyncEngineCfg sync_cfg_from(const WaystoneShellConfig& cfg, const char* device_id) {
    SyncEngineCfg c;
    c.conflict_policy = (int)cfg.conflict_policy;
    c.safety_backup = cfg.safety_backup;
    c.device_id = device_id;
    return c;
}
WebDavSession* webdav_session_begin(const WebDavCfg&) {
    assert(g_depth == 0);
    if (g_session_fail) return 0;
    g_events.push_back("begin");
    return reinterpret_cast<WebDavSession*>(&g_sess_token);
}
void webdav_session_end(WebDavSession*) { g_events.push_back("end"); }

std::vector<SaveDecision> sync_scan_title(const WsVault*, const void*, const char* title_name,
                                          const ShellOps&, const SyncEngineCfg&, int policy,
                                          WebDavSession* dav, bool*, const SyncProgress*) {
    assert(g_depth == 0 && dav && policy == 1);
    std::string name(title_name);
    g_events.push_back("scan:" + name);
    if (g_on_scan) g_on_scan(name);
    return g_scan[name];
}
int sync_push_group(const WsVault*, const void*, const char*, const ShellOps&,
                    const SyncEngineCfg&, const std::string& group_key, WebDavSession* dav,
                    const SyncProgress*) {
    assert(g_depth == 0 && dav);
    g_events.push_back("push:" + group_key);
    return g_push_rc;
}
int sync_pull_hash(const WsVault*, const void*, const ShellOps&, const SyncEngineCfg&,
                   const std::string& base_path, const std::string& group_key,
                   const std::string& hash, const std::string& head_mtime,
                   const std::vector<uint8_t>& raw_tree, WebDavSession* dav,
                   const SyncProgress*) {
    assert(g_depth == 0 && dav);
    char n[32];
    snprintf(n, sizeof(n), "%zu", raw_tree.size());
    g_events.push_back("pull:" + group_key + ":" + base_path + ":" + hash + ":" + head_mtime + ":" + n);
    return g_pull_rc;
}

static void prepare(FakeTitle&) { g_prepared++; }

static FakeTitle title(const char* name, bool remote) {
    FakeTitle t;
    t.title_id = 0x0004000000123400ULL;
    t.name = name;
    t.has_remote = remote;
    return t;
}
static SaveDecision conflict(const char* gk, size_t raw) {
    SaveDecision d;
    d.decision_type = "conflict_needs_input";
    d.group_key = gk;
    d.local_hash = std::string("L-") + gk;
    d.local_mtime = "2026-01-01T00:00:00Z";
    d.head_hash = std::string("R-") + gk;
    d.head_device_id = "other";
    d.head_mtime = "2026-01-02T00:00:00Z";
    d.base_path = std::string("bp/") + gk;
    d.raw_tree.assign(raw, 7);
    return d;
}
static SaveDecision in_sync(const char* gk) {
    SaveDecision d;
    d.decision_type = "in_sync";
    d.group_key = gk;
    return d;
}
static Core* make_core(const std::vector<FakeTitle>& titles) {
    static ShellOps ops = {0, 0, 0, 0};
    WebDavCfg dav = {"http://dav", "user", "secret"};
    WaystoneShellConfig cfg;
    ConflictLockOps lk = {t_lock, t_unlock, 0};
    g_core = new Core(0, "me", dav, titles, cfg, &ops, lk, prepare);
    return g_core;
}
static int find_event(const std::string& e) {
    for (size_t i = 0; i < g_events.size(); i++) if (g_events[i] == e) return (int)i;
    return -1;
}
static int count_prefix(const std::string& p) {
    int n = 0;
    for (size_t i = 0; i < g_events.size(); i++) if (g_events[i].compare(0, p.size(), p) == 0) n++;
    return n;
}
static std::vector<FakeTitle> abc(bool b_remote) {
    std::vector<FakeTitle> ts;
    ts.push_back(title("A", true));
    ts.push_back(title("B", b_remote));
    ts.push_back(title("C", true));
    return ts;
}

static void test_scan_collects_conflicts_with_stable_ids() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["A"].push_back(in_sync("3ds/a/extdata"));
    g_scan["C"].push_back(conflict("3ds/c/main", 4));
    Core* c = make_core(abc(false));
    assert(c->phase() == ConflictPhase::Idle && c->version() == 0 && !c->is_running());
    assert(c->begin_scan());
    assert(c->is_running() && c->phase() == ConflictPhase::Scanning && c->version() == 1);
    assert(!c->begin_scan());                               // one worker at a time
    c->run_scan();
    assert(!c->is_running() && c->phase() == ConflictPhase::Ready);
    assert(find_event("scan:B") == -1);                     // !has_remote skipped
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 2 && v[0].id == 1 && v[1].id == 2);
    assert(v[0].title_name == "A" && v[0].group_key == "3ds/a/main" && !v[0].queued);
    assert(v[0].local_hash == "L-3ds/a/main" && v[0].local_mtime == "2026-01-01T00:00:00Z");
    assert(v[0].remote_hash == "R-3ds/a/main" && v[0].remote_device_id == "other");
    assert(v[0].remote_mtime == "2026-01-02T00:00:00Z");
    assert(c->status() == "Scan complete: 2 conflict(s) found");
    assert(c->version() == 3);                              // clear + 2 adds
    assert(g_prepared == 2);
    assert(g_events.front() == "begin" && g_events.back() == "end");
    assert(g_depth == 0 && g_lock_calls > 0);
    assert(c->begin_scan());
    c->run_scan();
    v = c->views();
    assert(v.size() == 2 && v[0].id == 3 && v[1].id == 4);  // ids never reused
    delete c;
    printf("test_scan_collects_conflicts_with_stable_ids PASSED\n");
}

static void enqueue_id1_on_b(const std::string& name) {
    if (name != "B") return;
    uint32_t v0 = g_core->version();
    assert(!g_core->begin_resolve(1, true));                // queued for the scan thread
    assert(g_core->version() == v0 + 1);
    assert(g_core->views()[0].queued);
    assert(!g_core->begin_resolve(1, false));               // already queued -> ignored
    assert(!g_core->begin_resolve(99, true));               // unknown -> ignored
    assert(g_core->version() == v0 + 1);
}

static void test_queue_while_scanning_drains_between_titles() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["B"].push_back(conflict("3ds/b/main", 8));
    g_scan["C"].push_back(conflict("3ds/c/main", 8));
    g_on_scan = enqueue_id1_on_b;
    Core* c = make_core(abc(true));
    assert(c->begin_scan());
    c->run_scan();
    int push = find_event("push:3ds/a/main");
    assert(push > find_event("scan:B") && push < find_event("scan:C"));
    assert(count_prefix("push:") == 1 && count_prefix("pull:") == 0);
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 2 && v[0].id == 2 && v[1].id == 3);  // id 1 erased, others keep their ids
    assert(c->status() == "Scan complete: 2 conflict(s) found");
    assert(c->phase() == ConflictPhase::Ready && !c->is_running());
    delete c;
    printf("test_queue_while_scanning_drains_between_titles PASSED\n");
}

static void test_failed_drain_unqueues_and_counts_error() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["B"].push_back(conflict("3ds/b/main", 8));
    g_scan["C"].push_back(conflict("3ds/c/main", 8));
    g_on_scan = enqueue_id1_on_b;
    g_push_rc = -1;
    Core* c = make_core(abc(true));
    assert(c->begin_scan());
    c->run_scan();
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 3 && v[0].id == 1 && !v[0].queued);
    assert(c->status() == "Scan complete: 3 conflict(s), 1 resolve error(s)");
    delete c;
    printf("test_failed_drain_unqueues_and_counts_error PASSED\n");
}

static void enqueue_id1_on_c(const std::string& name) {
    if (name != "C") return;
    assert(!g_core->begin_resolve(1, false));               // queued during the last title
}

static void test_close_loop_drains_resolve_queued_on_last_title() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["C"].push_back(conflict("3ds/c/main", 8));
    g_on_scan = enqueue_id1_on_c;
    Core* c = make_core(abc(false));
    assert(c->begin_scan());
    c->run_scan();
    int pull = find_event("pull:3ds/a/main:bp/3ds/a/main:R-3ds/a/main:2026-01-02T00:00:00Z:8");
    assert(pull > find_event("scan:C") && pull < find_event("end"));
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 1 && v[0].id == 2);
    assert(c->status() == "Scan complete: 1 conflict(s) found");
    assert(c->phase() == ConflictPhase::Ready && !c->is_running());
    assert(c->begin_resolve(2, true));                      // queue closed -> resolve thread
    c->run_resolve(2, true);
    delete c;
    printf("test_close_loop_drains_resolve_queued_on_last_title PASSED\n");
}

static void enqueue_then_cancel_on_b(const std::string& name) {
    if (name != "B") return;
    assert(!g_core->begin_resolve(1, true));
    g_core->request_cancel();
}

static void test_cancel_drops_queue_and_next_scan_resets_it() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["B"].push_back(conflict("3ds/b/main", 8));
    g_scan["C"].push_back(conflict("3ds/c/main", 8));
    g_on_scan = enqueue_then_cancel_on_b;
    Core* c = make_core(abc(true));
    assert(c->begin_scan());
    c->run_scan();
    assert(count_prefix("push:") == 0);                     // drain returns at once when cancelled
    assert(find_event("scan:C") == -1);                     // stops before the next title
    assert(!c->is_running() && c->phase() == ConflictPhase::Ready);
    assert(c->views().size() == 2);
    g_on_scan = 0;
    g_events.clear();
    assert(c->begin_scan());                                // begin_scan clears cancel
    c->run_scan();
    assert(find_event("scan:C") >= 0 && c->views().size() == 3);
    delete c;
    printf("test_cancel_drops_queue_and_next_scan_resets_it PASSED\n");
}

static void test_busy_status_and_keep_local_resolve() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    g_scan["C"].push_back(conflict("3ds/c/main", 8));
    Core* c = make_core(abc(false));
    assert(c->begin_scan());
    c->run_scan();
    uint32_t v0 = c->version();
    assert(c->begin_resolve(1, true));
    assert(c->is_running() && c->phase() == ConflictPhase::Resolving);
    assert(!c->begin_resolve(2, false));                    // CAS lost
    assert(c->status() == kBusy);
    assert(!c->begin_scan());
    c->run_resolve(1, true);
    assert(find_event("push:3ds/a/main") >= 0);
    assert(!c->is_running() && c->phase() == ConflictPhase::Ready);
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 1 && v[0].id == 2);
    assert(c->status() == "Resolved (kept local): 3ds/a/main");
    assert(c->version() == v0 + 1);
    delete c;
    printf("test_busy_status_and_keep_local_resolve PASSED\n");
}

static void test_keep_remote_hands_tree_back_on_failure() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    std::vector<FakeTitle> ts;
    ts.push_back(title("A", true));
    Core* c = make_core(ts);
    assert(c->begin_scan());
    c->run_scan();
    const std::string pull = "pull:3ds/a/main:bp/3ds/a/main:R-3ds/a/main:2026-01-02T00:00:00Z:8";
    g_pull_rc = -1;
    assert(c->begin_resolve(1, false));
    c->run_resolve(1, false);
    assert(find_event(pull) >= 0);                          // tree borrowed, not empty
    assert(c->status() == "Error restoring: 3ds/a/main");
    assert(c->views().size() == 1 && !c->views()[0].queued);
    assert(c->phase() == ConflictPhase::Ready);
    g_events.clear();
    g_pull_rc = 0;
    assert(c->begin_resolve(1, false));
    c->run_resolve(1, false);
    assert(find_event(pull) >= 0);                          // handed back after the failure
    assert(c->views().empty() && c->phase() == ConflictPhase::Done);
    assert(c->status() == "Resolved (kept remote): 3ds/a/main");
    delete c;
    printf("test_keep_remote_hands_tree_back_on_failure PASSED\n");
}

static void test_session_and_thread_start_failures() {
    reset();
    g_scan["A"].push_back(conflict("3ds/a/main", 8));
    std::vector<FakeTitle> ts;
    ts.push_back(title("A", true));
    Core* c = make_core(ts);
    g_session_fail = true;
    assert(c->begin_scan());
    c->run_scan();
    assert(c->phase() == ConflictPhase::Error && !c->is_running());
    assert(c->status() == "Network init failed" && find_event("scan:A") == -1);
    g_session_fail = false;
    assert(c->begin_scan());
    c->fail_start("Thread creation failed", true);
    assert(c->phase() == ConflictPhase::Error && !c->is_running());
    assert(c->status() == "Thread creation failed");
    assert(c->begin_scan());
    c->run_scan();
    assert(c->views().size() == 1);
    assert(c->begin_resolve(1, true));
    c->fail_start("Thread creation failed", false);
    assert(c->phase() == ConflictPhase::Error && !c->is_running());
    g_session_fail = true;
    assert(c->begin_resolve(1, true));
    c->run_resolve(1, true);
    assert(c->status() == "Network init failed" && c->phase() == ConflictPhase::Ready);
    assert(c->views().size() == 1 && !c->is_running());
    delete c;
    printf("test_session_and_thread_start_failures PASSED\n");
}

static void test_debug_seed_and_resolve_stay_offline() {
    reset();
    Core* c = make_core(std::vector<FakeTitle>());
    std::vector<ConflictView> seed(2);
    seed[0].title_name = "X"; seed[0].group_key = "sw/x/main"; seed[0].local_mtime = "m";
    seed[1].title_name = "Y"; seed[1].group_key = "sw/y/main";
    c->debug_seed(seed, "Scan complete: 2 conflict(s) found");
    std::vector<ConflictView> v = c->views();
    assert(v.size() == 2 && v[0].id == 1 && v[1].id == 2 && v[0].local_mtime == "m");
    assert(c->phase() == ConflictPhase::Ready);
    assert(c->status() == "Scan complete: 2 conflict(s) found");
    assert(c->debug_resolve(1, false));
    assert(c->status() == "Resolved (kept remote): sw/x/main" && c->views().size() == 1);
    assert(!c->debug_resolve(1, true));
    assert(c->debug_resolve(2, true) && c->phase() == ConflictPhase::Done);
    assert(g_events.empty());
    delete c;
    printf("test_debug_seed_and_resolve_stay_offline PASSED\n");
}

int main() {
    test_scan_collects_conflicts_with_stable_ids();
    test_queue_while_scanning_drains_between_titles();
    test_failed_drain_unqueues_and_counts_error();
    test_close_loop_drains_resolve_queued_on_last_title();
    test_cancel_drops_queue_and_next_scan_resets_it();
    test_busy_status_and_keep_local_resolve();
    test_keep_remote_hands_tree_back_on_failure();
    test_session_and_thread_start_failures();
    test_debug_seed_and_resolve_stay_offline();
    printf("ALL PASSED\n");
    return 0;
}
