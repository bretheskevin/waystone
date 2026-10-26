#pragma once
#include <string>
#include <vector>
#include "kit/ws_style.h"
#include "rom_parse.h"
#include "saves.h"
#include "sync_summary.h"

// Pure dashboard rules (no borealis): which titles a section shows, sidebar badge, tile extras.

enum class Section { Games, Emulator, Conflicts, Settings };

inline std::vector<size_t> section_title_indices(const std::vector<TitleInfo>& titles, Section s) {
    std::vector<size_t> out;
    if (s != Section::Games && s != Section::Emulator) return out;
    const TitleSource want = s == Section::Games ? SourceInstalled : SourceRom;
    for (size_t i = 0; i < titles.size(); i++)
        if (titles[i].source == want) out.push_back(i);
    return out;
}

// A finished conflict scan is authoritative; until then count conflicts of the last sync run.
inline int conflict_badge(int scan_known, const std::vector<TitleResult>& results) {
    if (scan_known >= 0) return scan_known;
    int n = 0;
    for (const TitleResult& r : results)
        if (r.state == TitleState::Conflict) n++;
    return n;
}

inline TileMarker tile_marker(TitleState s) {
    if (s == TitleState::Conflict) return TileMarker::Conflict;
    if (s == TitleState::Failed) return TileMarker::Failed;
    return TileMarker::None;
}

inline std::string tile_state_text(const TitleResult* r) {
    if (!r || r->state == TitleState::Pending) return "Not synced yet";
    if (r->state == TitleState::Conflict) return "Conflict";
    if (r->state == TitleState::Failed && !r->reason.empty()) return "Failed: " + r->reason;
    return title_state_label(r->state);
}

inline std::string tile_detail(const TitleInfo& t, const TitleResult* r) {
    if (t.source == SourceRom)
        return std::string(rom_system_badge(t.system)) + " \xc2\xb7 " + tile_state_text(r);
    return tile_state_text(r);
}
