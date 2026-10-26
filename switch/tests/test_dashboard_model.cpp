// Host-only test for the dashboard section/marker/detail rules.
// Build (from repo root):
//   g++ -std=c++17 -Wall -Wextra -I switch/source -I switch/source/ui -I shell-common \
//     -I switch/preview/shims switch/tests/test_dashboard_model.cpp shell-common/sync_summary.cpp \
//     shell-common/rom_parse.cpp -o /tmp/test_dashboard_model && /tmp/test_dashboard_model
#include "dashboard_model.h"
#include <cassert>
#include <cstdio>

static TitleInfo installed(const char* name) {
    TitleInfo t{};
    t.name   = name;
    t.source = SourceInstalled;
    return t;
}

static TitleInfo rom(const char* name, const char* system) {
    TitleInfo t{};
    t.name   = name;
    t.source = SourceRom;
    t.system = system;
    return t;
}

static TitleResult result(TitleState s, const char* reason = "") {
    TitleResult r;
    r.state  = s;
    r.reason = reason;
    return r;
}

static void test_section_title_indices() {
    const std::vector<TitleInfo> titles = {installed("A"), rom("B", "gba"), installed("C")};
    assert((section_title_indices(titles, Section::Games) == std::vector<size_t>{0, 2}));
    assert((section_title_indices(titles, Section::Emulator) == std::vector<size_t>{1}));
    assert(section_title_indices(titles, Section::Conflicts).empty());
    assert(section_title_indices(titles, Section::Settings).empty());
    printf("test_section_title_indices PASSED\n");
}

static void test_conflict_badge() {
    assert(conflict_badge(-1, {result(TitleState::Conflict), result(TitleState::InSync),
                               result(TitleState::Conflict)}) == 2);
    assert(conflict_badge(0, {result(TitleState::Conflict)}) == 0);
    assert(conflict_badge(5, {}) == 5);
    assert(conflict_badge(-1, {}) == 0);
    printf("test_conflict_badge PASSED\n");
}

static void test_tile_marker() {
    assert(tile_marker(TitleState::Conflict) == TileMarker::Conflict);
    assert(tile_marker(TitleState::Failed) == TileMarker::Failed);
    assert(tile_marker(TitleState::InSync) == TileMarker::None);
    assert(tile_marker(TitleState::Pending) == TileMarker::None);
    printf("test_tile_marker PASSED\n");
}

static void test_tile_detail() {
    assert(tile_detail(rom("B", "gba"), nullptr) == "GBA \xc2\xb7 Not synced yet");
    const TitleResult failed = result(TitleState::Failed, "timeout");
    assert(tile_detail(installed("A"), &failed) == "Failed: timeout");
    const TitleResult pending = result(TitleState::Pending);
    assert(tile_detail(installed("A"), &pending) == "Not synced yet");
    const TitleResult conflict = result(TitleState::Conflict);
    assert(tile_detail(installed("A"), &conflict) == "Conflict");
    const TitleResult updown = result(TitleState::UpDown);
    assert(tile_detail(rom("D", "nds"), &updown) == "DS \xc2\xb7 Uploaded + downloaded");
    const TitleResult bare_fail = result(TitleState::Failed);
    assert(tile_detail(installed("A"), &bare_fail) == "Failed");
    printf("test_tile_detail PASSED\n");
}

int main() {
    test_section_title_indices();
    test_conflict_badge();
    test_tile_marker();
    test_tile_detail();
    printf("ALL PASSED\n");
    return 0;
}
