// Host-only test for the sync progress/summary rules.
// Build (from repo root):
//   g++ -std=c++11 -fno-exceptions -fno-rtti -Wall -Wextra -I 3ds/source \
//     3ds/tests/test_sync_summary.cpp 3ds/source/sync_summary.cpp -o /tmp/test_sync_summary
#include "sync_summary.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static bool feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }

static void test_final_state_precedence() {
    TitleTally t;
    assert(final_title_state(t) == TitleState::InSync);
    t.uploaded = true;
    assert(final_title_state(t) == TitleState::Uploaded);
    t.uploaded = false; t.downloaded = true;
    assert(final_title_state(t) == TitleState::Downloaded);
    t.uploaded = true;
    assert(final_title_state(t) == TitleState::UpDown);
    t.conflict = true;
    assert(final_title_state(t) == TitleState::Conflict);
    t.pull_failed = true;
    assert(final_title_state(t) == TitleState::Failed);
    TitleTally p; p.push_failed = true; p.downloaded = true;
    assert(final_title_state(p) == TitleState::Failed);
    printf("test_final_state_precedence PASSED\n");
}

static TitleResult r(TitleState s) { TitleResult x; x.state = s; return x; }

static void test_headline() {
    std::vector<TitleResult> v;
    assert(format_sync_headline(v) == "Done \xe2\x80\x94 nothing to sync");

    v.push_back(r(TitleState::InSync));
    v.push_back(r(TitleState::InSync));
    v.push_back(r(TitleState::InSync));
    assert(format_sync_headline(v) == "Done \xe2\x80\x94 3 synced");

    v.clear();
    v.push_back(r(TitleState::InSync));
    v.push_back(r(TitleState::InSync));
    v.push_back(r(TitleState::UpDown));
    v.push_back(r(TitleState::Downloaded));
    v.push_back(r(TitleState::Conflict));
    v.push_back(r(TitleState::Failed));
    assert(format_sync_headline(v) ==
           "Done \xe2\x80\x94 2 synced, 1 uploaded, 2 downloaded, 1 conflict, 1 failed");

    v.clear();
    v.push_back(r(TitleState::Conflict));
    v.push_back(r(TitleState::Conflict));
    v.push_back(r(TitleState::Uploaded));
    assert(format_sync_headline(v) == "Done \xe2\x80\x94 1 uploaded, 2 conflicts");
    printf("test_headline PASSED\n");
}

static void test_combined_progress() {
    assert(feq(combined_progress(0, 0, 4, 0, 0), 0.0f));
    assert(feq(combined_progress(0, 1, 2, 0, 0), 0.25f));
    assert(feq(combined_progress(1, 3, 4, 50, 100), 0.9375f));
    assert(feq(combined_progress(1, 0, 2, 200, 100), 0.75f));   // got > total clamps frac to 1
    assert(feq(combined_progress(0, -1, 4, 0, 0), 0.0f));        // no active title yet
    assert(feq(combined_progress(0, 0, 0, 0, 0), 0.0f));         // no titles
    assert(feq(combined_progress(0, 2, 4, 10, 0), 0.25f));       // unknown total -> frac 0
    printf("test_combined_progress PASSED\n");
}

static void test_next_xfer_total() {
    assert(next_xfer_total(0, 0) == 0);
    assert(next_xfer_total(0, 1024) == 1024);
    assert(next_xfer_total(1024, 0) == 1024);      // curl tick with unknown total keeps the seed
    assert(next_xfer_total(1024, 2048) == 2048);
    printf("test_next_xfer_total PASSED\n");
}

static void test_labels() {
    assert(strcmp(title_state_label(TitleState::InSync), "In sync") == 0);
    assert(strcmp(title_state_label(TitleState::Conflict),
                  "Conflict \xe2\x80\x94 resolve manually") == 0);
    assert(strcmp(title_state_label(TitleState::Failed), "Failed") == 0);
    printf("test_labels PASSED\n");
}

int main() {
    test_final_state_precedence();
    test_headline();
    test_combined_progress();
    test_next_xfer_total();
    test_labels();
    printf("ALL PASSED\n");
    return 0;
}
