// Host-only test for the "refuse while busy" decision shared by restore / resolve / credential edits.
// Build (from repo root):
//   g++ -std=c++11 -Wall -Wextra -I switch/source/ui switch/tests/test_busy_policy.cpp \
//     -o /tmp/test_busy_policy && /tmp/test_busy_policy
#include "busy_policy.h"
#include <cassert>
#include <cstdio>

static bool refused(BusyScope s, bool sync, bool conflict, size_t reaping) {
    return busy_refusal(s, sync, conflict, reaping) != nullptr;
}

static void test_restore() {
    assert(refused(BusyScope::Restore, true, false, 0));
    assert(refused(BusyScope::Restore, false, true, 0));
    assert(refused(BusyScope::Restore, false, false, 1));
    assert(!refused(BusyScope::Restore, false, false, 0));
}

static void test_resolve_allows_own_scan() {
    assert(refused(BusyScope::Resolve, true, false, 0));
    assert(refused(BusyScope::Resolve, false, false, 2));
    assert(!refused(BusyScope::Resolve, false, true, 0));
    assert(!refused(BusyScope::Resolve, false, false, 0));
}

static void test_credentials() {
    assert(refused(BusyScope::Credentials, true, false, 0));
    assert(refused(BusyScope::Credentials, false, true, 0));
    assert(refused(BusyScope::Credentials, false, false, 1));
    assert(!refused(BusyScope::Credentials, false, false, 0));
}

static void test_sync_reason_wins() {
    const char* a = busy_refusal(BusyScope::Restore, true, true, 3);
    const char* b = busy_refusal(BusyScope::Restore, true, false, 0);
    assert(a && b && a == b);
}

int main() {
    test_restore();
    test_resolve_allows_own_scan();
    test_credentials();
    test_sync_reason_wins();
    printf("test_busy_policy: all passed\n");
    return 0;
}
