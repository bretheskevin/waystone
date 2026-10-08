// Host-only test for the decide-first sync engine: fake FFI + in-memory WebDAV.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common -I ffi/include \
//     shell-common/tests/test_sync_engine.cpp shell-common/sync_engine.cpp \
//     shell-common/sync_rules.cpp shell-common/sync_summary.cpp shell-common/json.cpp \
//     shell-common/file_tree.cpp \
//     -o /tmp/test_sync_engine && /tmp/test_sync_engine
#include "sync_engine.h"
#include "json.h"
#include "snapshot.h"
#include "snapshot_browse.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

struct Vault;
extern "C" {
#include "waystone.h"
}

static std::map<std::string, std::vector<uint8_t> > g_files;
static std::set<std::string> g_dirs;
static std::vector<std::string> g_events;
static std::vector<std::string> g_decisions;   // scripted ws_decide_pull results (FIFO)
static std::vector<int> g_policy_args;
static std::set<std::string> g_propfind_fail;
static int g_live = 0;                          // outstanding fake FFI allocations

static void reset() {
    g_files.clear(); g_dirs.clear(); g_events.clear(); g_decisions.clear();
    g_policy_args.clear(); g_propfind_fail.clear();
}
static char* dup_c(const std::string& s) {
    char* p = (char*)malloc(s.size() + 1);
    memcpy(p, s.c_str(), s.size() + 1);
    g_live++;
    return p;
}
static WsBuf dup_buf(const uint8_t* d, size_t n) {
    WsBuf b;
    b.ptr = (uint8_t*)malloc(n ? n : 1);
    if (n) memcpy(b.ptr, d, n);
    b.len = n;
    g_live++;
    return b;
}
static std::string parent_of(const std::string& p) {
    size_t s = p.rfind('/');
    return s == std::string::npos ? std::string() : p.substr(0, s);
}
static std::string file_str(const std::string& p) {
    std::map<std::string, std::vector<uint8_t> >::iterator it = g_files.find(p);
    if (it == g_files.end()) return "";
    return std::string(it->second.begin(), it->second.end());
}

extern "C" {
void ws_buf_free(WsBuf b) { if (b.ptr) { free(b.ptr); g_live--; } }
void ws_string_free(char* s) { if (s) { free(s); g_live--; } }
const char* ws_last_error(void) { return "fake"; }
char* ws_vault_path_segment(const WsVault*, const char* name) { return dup_c(name); }
char* ws_vault_blob_name(const WsVault*, const char* hash) { return dup_c(std::string("blob-") + hash); }
WsBuf ws_vault_encrypt_blob(const WsVault*, const uint8_t* d, uintptr_t n) { return dup_buf(d, n); }
WsBuf ws_vault_decrypt_blob(const WsVault*, const uint8_t* d, uintptr_t n) { return dup_buf(d, n); }
WsBuf ws_vault_encrypt_heads(const WsVault*, const uint8_t* d, uintptr_t n) { return dup_buf(d, n); }
WsBuf ws_vault_decrypt_heads(const WsVault*, const uint8_t* d, uintptr_t n) { return dup_buf(d, n); }
WsBuf ws_unzip(const uint8_t* d, uintptr_t n) { return dup_buf(d, n); }
char* ws_package(const char* meta, const uint8_t* files, uintptr_t n, WsBuf* out_zip) {
    std::string gk = json_get_string(meta, "group_key");
    std::string h = json_get_string(meta, "hash");
    *out_zip = dup_buf(files, n);
    return dup_c("{\"group_key\":\"" + gk + "\",\"content\":{\"hash\":\"" + h + "\"}}");
}
char* ws_fold_heads(const char* heads) {
    std::vector<std::string> v = json_split_array(heads);
    if (v.empty()) return 0;
    std::string best_h, best_m, best_d;
    for (size_t i = 0; i < v.size(); i++) {
        std::string m = json_get_string(v[i].c_str(), "mtime");
        if (best_m.empty() || m >= best_m) {
            best_m = m;
            best_h = json_get_string(v[i].c_str(), "hash");
            best_d = json_get_string(v[i].c_str(), "device_id");
        }
    }
    return dup_c("{\"device_id\":\"" + best_d + "\",\"hash\":\"" + best_h + "\",\"mtime\":\"" + best_m + "\"}");
}
char* ws_decide_pull(const char*, const char*, const char*, const char*, int policy) {
    g_events.push_back("decide");
    g_policy_args.push_back(policy);
    if (g_decisions.empty()) return 0;
    std::string d = g_decisions.front();
    g_decisions.erase(g_decisions.begin());
    return dup_c(d);
}
}

int webdav_put_s(WebDavSession*, const char* path, const uint8_t* d, size_t n,
                 bool (*)(size_t, size_t, void*), void*) {
    std::string p(path);
    if (!g_dirs.count(parent_of(p))) { g_events.push_back("put409:" + p); return 409; }
    g_files[p] = std::vector<uint8_t>(d, d + n);
    g_events.push_back("put:" + p);
    return 0;
}
int webdav_get_s(WebDavSession*, const char* path, std::vector<uint8_t>* out,
                 bool (*)(size_t, size_t, void*), void*) {
    std::map<std::string, std::vector<uint8_t> >::iterator it = g_files.find(path);
    if (it == g_files.end()) return 1;
    *out = it->second;
    return 0;
}
int webdav_exists_s(WebDavSession*, const char* path) { return g_files.count(path) ? 1 : 0; }
int webdav_mkdir_p_s(WebDavSession*, const char* path) {
    std::string p(path);
    while (!p.empty()) { g_dirs.insert(p); p = parent_of(p); }
    g_events.push_back(std::string("mkdir:") + path);
    return 0;
}
int webdav_propfind_s(WebDavSession*, const char* path, std::vector<std::string>* out) {
    std::string p(path);
    if (g_propfind_fail.count(p)) return -1;
    out->clear();
    for (std::map<std::string, std::vector<uint8_t> >::iterator it = g_files.begin();
         it != g_files.end(); ++it)
        if (parent_of(it->first) == p) out->push_back(it->first);
    return 0;
}

std::string snapshot_sanitize_key(const std::string& k) { return k; }
bool write_snapshot(const char* dir, const uint8_t*, size_t) {
    g_events.push_back(std::string("snapshot:") + dir);
    return true;
}
bool snapshot_prune(const char*, int) { return true; }
std::string history_timestamp() { return "20260101T000000Z"; }

struct FakeTitle {
    std::vector<LocalSave> saves;
    std::vector<uint8_t> files;
    std::vector<uint8_t> raw;
    int list_rc;
    FakeTitle() : files(4, 7), raw(8, 1), list_rc(0) {}
};
static int fake_list(void*, const void* t, LocalSaveSet& out) {
    const FakeTitle* ft = static_cast<const FakeTitle*>(t);
    if (ft->list_rc) return ft->list_rc;
    out.raw_tree = ft->raw;
    for (size_t i = 0; i < ft->saves.size(); i++) {
        LocalSave s = ft->saves[i];
        s.files_ptr = ft->files.data();
        s.files_len = ft->files.size();
        out.saves.push_back(s);
    }
    return 0;
}
static int fake_write(void*, const void*, const std::string& gk, const uint8_t*, size_t) {
    g_events.push_back("write:" + gk);
    return 0;
}
static ShellOps fake_ops() {
    ShellOps o;
    o.list_saves = fake_list;
    o.list_remote_only = 0;
    o.write_save = fake_write;
    o.ctx = 0;
    return o;
}
static LocalSave save(const char* gk, const char* hash, const char* mtime) {
    LocalSave s;
    s.meta_json = std::string("{\"group_key\":\"") + gk + "\",\"hash\":\"" + hash + "\",\"mtime\":\"\"}";
    s.local_mtime = mtime;
    return s;
}
static void seed_head(const std::string& base, const char* dev, const char* hash, const char* mtime) {
    std::string j = build_device_head_json(dev, hash, mtime);
    g_files[base + "/heads/" + dev + ".json"] = std::vector<uint8_t>(j.begin(), j.end());
    webdav_mkdir_p_s(0, (base + "/heads").c_str());
}
static void seed_blob(const std::string& base, const char* hash, const char* body) {
    std::string b(body);
    g_files[base + "/blobs/blob-" + hash + ".bin"] = std::vector<uint8_t>(b.begin(), b.end());
    webdav_mkdir_p_s(0, (base + "/blobs").c_str());
}
static SyncEngineCfg cfg(int policy, bool backup) {
    SyncEngineCfg c;
    c.conflict_policy = policy;
    c.safety_backup = backup;
    c.device_id = "me";
    return c;
}
static int first_event(const std::string& prefix) {
    for (size_t i = 0; i < g_events.size(); i++)
        if (g_events[i].compare(0, prefix.size(), prefix) == 0) return (int)i;
    return -1;
}
static int last_event(const std::string& prefix) {
    int r = -1;
    for (size_t i = 0; i < g_events.size(); i++)
        if (g_events[i].compare(0, prefix.size(), prefix) == 0) r = (int)i;
    return r;
}

static void test_first_upload_pushes() {
    reset();
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", "2026-01-01T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, true), 0, 0);
    assert(r.uploaded && !r.failed && !r.downloaded && !r.conflict);
    assert(first_event("decide") == -1);                 // no heads -> push without asking core
    assert(g_files.count("sw/game/main/blobs/blob-h1.bin"));
    assert(json_get_string(file_str("sw/game/main/heads/me.json").c_str(), "hash") == "h1");
    assert(first_event("put409:") >= 0 && first_event("mkdir:") >= 0);   // lazy parent creation
    assert(g_live == 0);
    printf("test_first_upload_pushes PASSED\n");
}

static void test_remote_newer_pulls_and_updates_base() {
    reset();
    seed_head("sw/game/main", "me", "h0", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h2", "2026-01-02T00:00:00Z");
    seed_blob("sw/game/main", "h2", "remote");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"pull\",\"head_hash\":\"h2\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h0", "2026-01-01T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, true), 0, 0);
    assert(r.downloaded && !r.failed && !r.uploaded);
    assert(first_event("snapshot:") >= 0 && first_event("snapshot:") < first_event("write:sw/game/main"));
    std::string me = file_str("sw/game/main/heads/me.json");
    assert(json_get_string(me.c_str(), "hash") == "h2");
    assert(json_get_string(me.c_str(), "mtime") == "2026-01-02T00:00:00Z");
    assert(g_live == 0);
    printf("test_remote_newer_pulls_and_updates_base PASSED\n");
}

static void test_conflict_resolved_remote_pulls_folded_head() {
    reset();
    seed_head("sw/game/main", "me", "h0", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h3", "2026-01-03T00:00:00Z");
    seed_blob("sw/game/main", "h3", "remote");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"conflict_resolved\",\"winner\":\"remote\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", "2026-01-02T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, true), 0, 0);
    assert(r.downloaded && !r.failed && !r.uploaded && !r.conflict);
    assert(first_event("write:sw/game/main") >= 0);
    std::string me = file_str("sw/game/main/heads/me.json");
    assert(json_get_string(me.c_str(), "hash") == "h3");
    assert(json_get_string(me.c_str(), "mtime") == "2026-01-03T00:00:00Z");
    assert(g_live == 0);
    printf("test_conflict_resolved_remote_pulls_folded_head PASSED\n");
}

static void test_safety_backup_off_skips_snapshot() {
    reset();
    seed_head("sw/game/main", "me", "h0", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h2", "2026-01-02T00:00:00Z");
    seed_blob("sw/game/main", "h2", "remote");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"pull\",\"head_hash\":\"h2\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h0", "2026-01-01T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, false), 0, 0);
    assert(r.downloaded);
    assert(first_event("snapshot:") == -1);
    assert(first_event("write:sw/game/main") >= 0);
    assert(g_live == 0);
    printf("test_safety_backup_off_skips_snapshot PASSED\n");
}

static void test_prompt_conflict_is_skipped() {
    reset();
    seed_head("sw/game/main", "me", "h0", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h2", "2026-01-02T00:00:00Z");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"conflict_needs_input\",\"local_hash\":\"h1\",\"remote_hash\":\"h2\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", "2026-01-03T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(1, true), 0, 0);
    assert(r.conflict && !r.failed && !r.uploaded && !r.downloaded);
    assert(first_event("put:") == -1 && first_event("write:") == -1);
    assert(final_title_state(r) == TitleState::Conflict);
    assert(g_live == 0);
    printf("test_prompt_conflict_is_skipped PASSED\n");
}

static void test_newest_wins_without_mtime_downgrades_to_prompt() {
    reset();
    seed_head("sw/game/main", "me", "h0", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h2", "2026-01-02T00:00:00Z");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"conflict_needs_input\",\"local_hash\":\"h1\",\"remote_hash\":\"h2\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", ""));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, true), 0, 0);
    assert(g_policy_args.size() == 1 && g_policy_args[0] == 1);
    assert(r.conflict);
    assert(g_live == 0);
    printf("test_newest_wins_without_mtime_downgrades_to_prompt PASSED\n");
}

static void test_decide_all_before_acting() {
    reset();
    seed_head("sw/a/main", "me", "hA0", "2026-01-01T00:00:00Z");
    seed_head("sw/b/main", "me", "hB0", "2026-01-01T00:00:00Z");
    seed_head("sw/b/main", "other", "hB2", "2026-01-02T00:00:00Z");
    seed_blob("sw/b/main", "hB2", "remote-b");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"push\"}");
    g_decisions.push_back("{\"type\":\"pull\",\"head_hash\":\"hB2\"}");
    FakeTitle t;
    t.saves.push_back(save("sw/a/main", "hA1", "2026-01-03T00:00:00Z"));
    t.saves.push_back(save("sw/b/main", "hB0", "2026-01-01T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "two", fake_ops(), cfg(0, true), 0, 0);
    assert(r.uploaded && r.downloaded && !r.failed);
    assert(final_title_state(r) == TitleState::UpDown);
    int last_decide = last_event("decide");
    assert(last_decide >= 0);
    assert(last_decide < first_event("put"));            // covers put: and put409:
    assert(last_decide < first_event("write:"));
    assert(g_live == 0);
    printf("test_decide_all_before_acting PASSED\n");
}

static void test_server_check_failure_continues() {
    reset();
    g_propfind_fail.insert("sw/a/main/heads");
    FakeTitle t;
    t.saves.push_back(save("sw/a/main", "hA1", "2026-01-03T00:00:00Z"));
    t.saves.push_back(save("sw/b/main", "hB1", "2026-01-03T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "two", fake_ops(), cfg(0, true), 0, 0);
    assert(r.failed && r.reason == "Server check failed");
    assert(r.uploaded);                                  // save b still pushed
    assert(json_get_string(file_str("sw/b/main/heads/me.json").c_str(), "hash") == "hB1");
    assert(g_live == 0);
    printf("test_server_check_failure_continues PASSED\n");
}

static void test_in_sync_repairs_stale_base() {
    reset();
    seed_head("sw/game/main", "me", "hOLD", "2026-01-01T00:00:00Z");
    seed_head("sw/game/main", "other", "h1", "2026-01-03T00:00:00Z");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"in_sync\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", "2026-01-03T00:00:00Z"));
    TitleTally r = sync_title(0, &t, "game", fake_ops(), cfg(0, true), 0, 0);
    assert(!r.failed && !r.uploaded && !r.downloaded && !r.conflict);
    std::string me = file_str("sw/game/main/heads/me.json");
    assert(json_get_string(me.c_str(), "hash") == "h1");
    assert(json_get_string(me.c_str(), "mtime") == "2026-01-03T00:00:00Z");
    assert(g_live == 0);
    printf("test_in_sync_repairs_stale_base PASSED\n");
}

static void test_list_failure_marks_title_failed() {
    reset();
    FakeTitle t; t.list_rc = -1;
    TitleTally r = sync_title(0, &t, "broken", fake_ops(), cfg(0, true), 0, 0);
    assert(r.failed && r.reason == "Read save failed");
    assert(g_live == 0);
    printf("test_list_failure_marks_title_failed PASSED\n");
}

static void test_push_group_pushes_only_that_save() {
    reset();
    FakeTitle t;
    t.saves.push_back(save("sw/a/main", "hA1", "2026-01-03T00:00:00Z"));
    t.saves.push_back(save("sw/b/main", "hB1", "2026-01-03T00:00:00Z"));
    int rc = sync_push_group(0, &t, "two", fake_ops(), cfg(1, true), "sw/b/main", 0, 0);
    assert(rc == 0);
    assert(g_files.count("sw/b/main/heads/me.json") && !g_files.count("sw/a/main/heads/me.json"));
    assert(sync_push_group(0, &t, "two", fake_ops(), cfg(1, true), "sw/zzz/main", 0, 0) != 0);
    assert(g_live == 0);
    printf("test_push_group_pushes_only_that_save PASSED\n");
}

static void test_scan_title_copies_raw_tree() {
    reset();
    seed_head("sw/game/main", "other", "h2", "2026-01-02T00:00:00Z");
    g_events.clear();
    g_decisions.push_back("{\"type\":\"conflict_needs_input\",\"local_hash\":\"h1\",\"remote_hash\":\"h2\"}");
    FakeTitle t; t.saves.push_back(save("sw/game/main", "h1", "2026-01-03T00:00:00Z"));
    bool err = true;
    std::vector<SaveDecision> d = sync_scan_title(0, &t, "game", fake_ops(), cfg(0, true), 1, 0, &err, 0);
    assert(!err && d.size() == 1);
    assert(d[0].decision_type == "conflict_needs_input");
    assert(d[0].raw_tree == t.raw);
    assert(d[0].head_mtime == "2026-01-02T00:00:00Z");
    assert(d[0].head_hash == "h2" && d[0].head_device_id == "other");
    assert(first_event("put") == -1);
    assert(g_live == 0);
    printf("test_scan_title_copies_raw_tree PASSED\n");
}

static void test_adopt_normalized_owns_or_frees_buffer() {
    reset();
    const uint8_t junk[3] = {1, 2, 3};
    WsBuf bad = dup_buf(junk, sizeof(junk));
    std::vector<uint8_t> raw(5, 9);
    {
        LocalSaveSet set;
        assert(!set.adopt_normalized(bad.ptr, bad.len, raw, "2026-01-01T00:00:00Z"));
        assert(set.saves.empty() && set.raw_tree.empty());
    }
    assert(raw.size() == 5);                             // caller keeps raw on failure
    assert(g_live == 0);                                 // freed exactly once on failure

    const uint8_t empty_list[4] = {0, 0, 0, 0};
    WsBuf ok = dup_buf(empty_list, sizeof(empty_list));
    WsBuf again = dup_buf(empty_list, sizeof(empty_list));
    {
        LocalSaveSet set;
        assert(set.adopt_normalized(ok.ptr, ok.len, raw, ""));
        assert(set.raw_tree.size() == 5 && raw.empty() && set.saves.empty());
        std::vector<uint8_t> raw2(2, 1);
        assert(!set.adopt_normalized(again.ptr, again.len, raw2, ""));  // second adopt refused + freed
        assert(g_live == 1);                             // first buffer still owned by the set
    }
    assert(g_live == 0);                                 // destructor freed it
    printf("test_adopt_normalized_owns_or_frees_buffer PASSED\n");
}

int main() {
    test_adopt_normalized_owns_or_frees_buffer();
    test_first_upload_pushes();
    test_remote_newer_pulls_and_updates_base();
    test_conflict_resolved_remote_pulls_folded_head();
    test_safety_backup_off_skips_snapshot();
    test_prompt_conflict_is_skipped();
    test_newest_wins_without_mtime_downgrades_to_prompt();
    test_decide_all_before_acting();
    test_server_check_failure_continues();
    test_in_sync_repairs_stale_base();
    test_list_failure_marks_title_failed();
    test_push_group_pushes_only_that_save();
    test_scan_title_copies_raw_tree();
    printf("ALL PASSED\n");
    return 0;
}
