// Host-only test for the snapshot/history display helpers.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I shell-common \
//     shell-common/tests/test_snapshot_browse.cpp shell-common/snapshot_browse.cpp \
//     shell-common/snapshot.cpp shell-common/file_tree.cpp \
//     -o /tmp/test_snapshot_browse && /tmp/test_snapshot_browse
#include "snapshot_browse.h"
#include <cassert>
#include <cstdio>

static void test_human_timestamp_compact() {
    assert(human_timestamp("20260914T183000Z") == "2026-09-14 18:30");
    assert(human_timestamp("20260914T183000.123Z") == "2026-09-14 18:30");
    printf("test_human_timestamp_compact PASSED\n");
}

static void test_human_timestamp_iso() {
    assert(human_timestamp("2026-09-14T18:30:00Z") == "2026-09-14 18:30");
    assert(human_timestamp("2026-09-14T18:30:00.250Z") == "2026-09-14 18:30");
    assert(human_timestamp("2026-09-14T18:30:00") == "2026-09-14 18:30");
    printf("test_human_timestamp_iso PASSED\n");
}

static void test_human_timestamp_unparseable() {
    assert(human_timestamp("") == "");
    assert(human_timestamp("time unknown") == "time unknown");
    assert(human_timestamp("2026-09-14") == "2026-09-14");
    assert(human_timestamp("2026-09-14X18:30:00Z") == "2026-09-14X18:30:00Z");
    printf("test_human_timestamp_unparseable PASSED\n");
}

int main() {
    test_human_timestamp_compact();
    test_human_timestamp_iso();
    test_human_timestamp_unparseable();
    printf("ALL PASSED\n");
    return 0;
}
