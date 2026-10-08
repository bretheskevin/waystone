// Host-only test for the pure decide-first sync rules.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_sync_rules.cpp shell-common/sync_rules.cpp shell-common/json.cpp \
//     -o /tmp/test_sync_rules && /tmp/test_sync_rules
#include "sync_rules.h"
#include <cassert>
#include <cstdio>

static void test_act_table() {
    assert(sync_act_for("in_sync", "") == SyncAct::None);
    assert(sync_act_for("push", "") == SyncAct::Push);
    assert(sync_act_for("pull", "") == SyncAct::Pull);
    assert(sync_act_for("conflict_resolved", "local") == SyncAct::Push);
    assert(sync_act_for("conflict_resolved", "remote") == SyncAct::Pull);
    assert(sync_act_for("conflict_resolved", "") == SyncAct::Unknown);
    assert(sync_act_for("conflict_needs_input", "") == SyncAct::Conflict);
    assert(sync_act_for("", "") == SyncAct::ScanFailed);
    assert(sync_act_for("bogus", "") == SyncAct::Unknown);
    printf("test_act_table PASSED\n");
}

static void test_effective_policy() {
    assert(effective_policy(0, true) == 0);   // NewestWins with a real mtime stays NewestWins
    assert(effective_policy(0, false) == 1);  // NewestWins without mtime -> Prompt
    assert(effective_policy(1, true) == 1);
    assert(effective_policy(1, false) == 1);
    printf("test_effective_policy PASSED\n");
}

static void test_classify_heads() {
    assert(classify_heads(0, 0, true) == HeadsOutcome::Push);     // first upload
    assert(classify_heads(0, 0, false) == HeadsOutcome::InSync);
    assert(classify_heads(2, 0, true) == HeadsOutcome::Failed);   // heads exist, none readable
    assert(classify_heads(2, 0, false) == HeadsOutcome::Failed);
    assert(classify_heads(2, 1, true) == HeadsOutcome::Decide);
    assert(classify_heads(1, 1, false) == HeadsOutcome::Decide);
    printf("test_classify_heads PASSED\n");
}

static void test_head_href() {
    assert(is_head_href("/dav/a/b/heads/dev1.json"));
    assert(!is_head_href("/dav/a/b/heads/"));
    assert(!is_head_href("x.jso"));
    assert(!is_head_href(""));
    printf("test_head_href PASSED\n");
}

static void test_mtime() {
    assert(utc_iso_from_epoch(0) == "1970-01-01T00:00:00Z");
    assert(utc_iso_from_epoch(1759881600LL) == "2025-10-08T00:00:00Z");
    const long long now = 1759881600LL;
    assert(plausible_mtime_iso(0, now) == "");                     // unset
    assert(plausible_mtime_iso(1483228799LL, now) == "");          // before 2017-01-01
    assert(plausible_mtime_iso(1483228800LL, now) == "2017-01-01T00:00:00Z");
    assert(plausible_mtime_iso(now - 3600, now) == "2025-10-07T23:00:00Z");
    assert(plausible_mtime_iso(now + 86400, now) == "2025-10-09T00:00:00Z");  // 1 day skew ok
    assert(plausible_mtime_iso(now + 86401, now) == "");          // further future -> unusable
    printf("test_mtime PASSED\n");
}

static void test_own_head_and_repair() {
    const std::string heads =
        "[{\"device_id\":\"other\",\"hash\":\"h2\",\"mtime\":\"2026-01-02T00:00:00Z\"},"
        "{\"device_id\":\"me\",\"hash\":\"h1\",\"mtime\":\"2026-01-01T00:00:00Z\"}]";
    assert(own_head_hash(heads, "me") == "h1");
    assert(own_head_hash(heads, "nobody") == "");
    assert(own_head_hash("[]", "me") == "");
    assert(base_needs_repair("in_sync", "h1", "h2"));
    assert(base_needs_repair("in_sync", "", "h2"));       // own head missing
    assert(!base_needs_repair("in_sync", "h2", "h2"));
    assert(!base_needs_repair("in_sync", "", ""));        // no local save
    assert(!base_needs_repair("pull", "h1", "h2"));
    printf("test_own_head_and_repair PASSED\n");
}

int main() {
    test_act_table();
    test_effective_policy();
    test_classify_heads();
    test_head_href();
    test_mtime();
    test_own_head_and_repair();
    printf("ALL PASSED\n");
    return 0;
}
