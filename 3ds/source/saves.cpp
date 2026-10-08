#include "saves.h"
#include "homebrew_filter.h"
#include "file_tree.h"
#include "net.h"
#include "snapshot_browse.h"
#include "rom_saves.h"
#include "rom_parse.h"
#include "sync_summary.h"
#include "remote_set.h"
#include <algorithm>
#include <map>
#include <set>

struct Vault;
extern "C" {
#include "waystone.h"
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <3ds.h>

// SMDH large-icon size: 48×48 pixels × 2 bytes/pixel (RGB565) = 4608 bytes.
static const size_t SMDH_SIZE     = 0x36C0;
static const size_t SMDH_ICON_OFF = 0x24C0;
static const size_t SMDH_ICON_LEN = 0x1200; // 4608 bytes

// Read SMDH metadata for the given title_id via the ExeFS "icon" file.
// On success, writes the real UTF-8 name into `name` and the 4608-byte
// RGB565 tiled icon into `icon`. On any failure, leaves both unchanged
// (caller pre-sets the hex-uid fallback name).
static void read_smdh(u64 tid, std::string& name, std::vector<uint8_t>& icon) {
    // Archive binary path: {low32, high32, mediatype, 0}
    u32 arch_data[4] = {
        static_cast<u32>(tid & 0xFFFFFFFF),
        static_cast<u32>(tid >> 32),
        static_cast<u32>(MEDIATYPE_SD),
        0
    };
    FS_Path arch_path = { PATH_BINARY, sizeof(arch_data), arch_data };

    // File binary path: ExeFS "icon" entry. The name is the 8-byte "icon\0\0\0\0",
    // i.e. TWO words {0x6E6F6369, 0}, so this lowpath must be 5 words / 0x14 bytes.
    // Dropping the trailing 0 (4 words / 0x10) makes FS reject it as InvalidArgument
    // (rc=0xE0E046BE) for every title. Matches Checkpoint smdh.cpp / FBI extractsmdh.c.
    u32 file_data[5] = { 0, 0, 0x2, 0x6E6F6369, 0 };
    FS_Path file_path = { PATH_BINARY, sizeof(file_data), file_data };

    Handle fh = 0;
    Result rc = FSUSER_OpenFileDirectly(
        &fh,
        ARCHIVE_SAVEDATA_AND_CONTENT,
        arch_path,
        file_path,
        FS_OPEN_READ,
        0
    );
    if (R_FAILED(rc)) {
        printf("[titles] smdh open failed tid=%016llX rc=0x%08lX (name/icon fallback)\n",
               (unsigned long long)tid, (unsigned long)rc);
        return;
    }

    uint8_t smdh[SMDH_SIZE];
    u32 bytes_read = 0;
    rc = FSFILE_Read(fh, &bytes_read, 0, smdh, static_cast<u32>(SMDH_SIZE));
    FSFILE_Close(fh);

    if (R_FAILED(rc) || bytes_read < static_cast<u32>(SMDH_SIZE)) {
        printf("[titles] smdh read failed tid=%016llX rc=0x%08lX bytes=%lu (name/icon fallback)\n",
               (unsigned long long)tid, (unsigned long)rc, (unsigned long)bytes_read);
        return;
    }

    // Verify "SMDH" magic at offset 0.
    if (memcmp(smdh, "SMDH", 4) != 0) {
        printf("[titles] smdh bad magic tid=%016llX (name/icon fallback)\n",
               (unsigned long long)tid);
        return;
    }

    // Extract the short description (first 0x40 UTF-16LE code units) from the
    // English title struct (index 1), then Japanese (index 0), then any non-empty.
    // Each title struct is 0x200 bytes; the short desc is the first 0x80 bytes.
    std::string resolved_name;
    int lang_order[] = { 1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    for (int li = 0; li < 16 && resolved_name.empty(); li++) {
        int lang = lang_order[li];
        const uint16_t* utf16 = reinterpret_cast<const uint16_t*>(
            smdh + 0x0008 + lang * 0x200
        );
        // Max UTF-8 output: 0x40 code units × 3 bytes each + null.
        uint8_t utf8_buf[0x40 * 3 + 1];
        memset(utf8_buf, 0, sizeof(utf8_buf));
        ssize_t written = utf16_to_utf8(utf8_buf, utf16, sizeof(utf8_buf) - 1);
        if (written <= 0) continue;
        utf8_buf[written] = '\0';
        // Trim trailing whitespace and null bytes.
        int end = static_cast<int>(written) - 1;
        while (end >= 0 && (utf8_buf[end] == 0 || utf8_buf[end] == ' ' ||
                             utf8_buf[end] == '\t' || utf8_buf[end] == '\r' ||
                             utf8_buf[end] == '\n')) {
            end--;
        }
        if (end >= 0) {
            resolved_name = std::string(reinterpret_cast<const char*>(utf8_buf),
                                        static_cast<size_t>(end + 1));
        }
    }

    if (!resolved_name.empty()) {
        name = resolved_name;
    }

    // Copy the 48×48 large icon RGB565 tiled data.
    icon.assign(smdh + SMDH_ICON_OFF, smdh + SMDH_ICON_OFF + SMDH_ICON_LEN);

    printf("[titles] smdh tid=%016llX name='%s' icon=%zuB\n",
           (unsigned long long)tid, name.c_str(), icon.size());
}

// Best-effort display name for titles whose SMDH cannot be read (corrupt or
// missing icon metadata -- these also show a broken icon/banner on the 3DS HOME
// menu). Factual title-ID -> retail-name data only (3dbrew title list; sibling
// IDs corroborated by the extdata_id_for quirks table below). Keyed on the low
// 32 bits. Returns 0 when the ID is not recognised.
static const char* known_title_name(u64 title_id) {
    static const struct { u32 low; const char* name; } NAMES[] = {
        { 0x00055D00, "Pokemon X" },
        { 0x00055E00, "Pokemon Y" },
        { 0x0011C400, "Pokemon Omega Ruby" },
        { 0x0011C500, "Pokemon Alpha Sapphire" },
        { 0x00164800, "Pokemon Sun" },
        { 0x00175E00, "Pokemon Moon" },
        { 0x001B5000, "Pokemon Ultra Sun" },
        { 0x001B5100, "Pokemon Ultra Moon" },
    };
    u32 low = static_cast<u32>(title_id & 0xFFFFFFFF);
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++)
        if (NAMES[i].low == low) return NAMES[i].name;
    return 0;
}

// Default display name before SMDH is read: a recognised retail name if we know
// the ID, otherwise the 5-hex-digit unique ID. read_smdh overwrites this with
// the real SMDH short-description on success.
static std::string default_title_name(u64 title_id) {
    const char* known = known_title_name(title_id);
    if (known) return std::string(known);
    char hex_uid[8];
    snprintf(hex_uid, sizeof(hex_uid), "%05X",
             (unsigned)((title_id >> 8) & 0xFFFFF));
    return std::string(hex_uid);
}

// Return the extdata archive ID for a given title_id.
// Quirks table transcribed verbatim from Checkpoint source/titlequirks.cpp
// (TitleQuirks::extdataIdFor, GPLv3, BernardoGiordano/FlagBrew).
// Factual game-title-ID → extdata-archive-ID mappings only; no code structure
// or comments copied. Default fallback (low >> 8) verified against 3dbrew
// "Extdata" page formula. All returned IDs are <= 20 bits except where noted.
u32 extdata_id_for(u64 title_id) {
    // Quirks: titles whose extdata archive ID differs from the default (low>>8).
    // Factual archive identifiers transcribed from the Checkpoint reference
    // (titlequirks.cpp) -- data only. Keyed on the low 32 bits of the title ID.
    static const struct { u32 low; u32 extdata_id; } EXTDATA_QUIRKS[] = {
        { 0x00055E00, 0x055D },  // Pokemon Y
        { 0x0011C400, 0x11C5 },  // Pokemon Omega Ruby
        { 0x00175E00, 0x1648 },  // Pokemon Moon
        { 0x00179600, 0x1794 },  // Fire Emblem Conquest SE NA
        { 0x00179800, 0x1794 },  // Fire Emblem Conquest SE NA
        { 0x00179700, 0x1795 },  // Fire Emblem Conquest SE EU
        { 0x0017A800, 0x1795 },  // Fire Emblem Conquest SE EU
        { 0x0012DD00, 0x12DC },  // Fire Emblem If JP
        { 0x0012DE00, 0x12DC },  // Fire Emblem If JP
        { 0x001B5100, 0x1B50 },  // Pokemon Ultramoon
    };
    u32 low = static_cast<u32>(title_id & 0xFFFFFFFF);
    for (size_t i = 0; i < sizeof(EXTDATA_QUIRKS)/sizeof(EXTDATA_QUIRKS[0]); i++) {
        if (EXTDATA_QUIRKS[i].low == low) {
            printf("[saves] extdata_id_for tid=%016llX -> quirks 0x%08lX\n",
                   (unsigned long long)title_id,
                   (unsigned long)EXTDATA_QUIRKS[i].extdata_id);
            return EXTDATA_QUIRKS[i].extdata_id;
        }
    }
    u32 unique = (low >> 8) & 0xFFFFF;
    printf("[saves] extdata_id_for tid=%016llX low=0x%08lX -> default 0x%05lX\n",
           (unsigned long long)title_id, (unsigned long)low, (unsigned long)unique);
    return unique;
}

SaveArchiveKind main_save_kind(const TitleInfo& title) {
    if (title.source == SourceRom) return SaveRomFile;
    if (title.is_twl) return SaveTwl;
    return SaveUser;
}

// System-title exclusion list transcribed data-only from Checkpoint
// 3ds/source/titlequirks.cpp (TitleQuirks::isSystemExcluded, GPLv3,
// BernardoGiordano/FlagBrew), fetched 2026-09-18. Factual title IDs only;
// no code structure or comments copied. Applied BEFORE read_smdh on the SD
// path (updates and system entries carry no save of their own; per-title
// SMDH reads on them would be wasted) and as a first-pass filter on the
// NAND path, which lists hundreds of system titles.
static bool is_system_excluded(u64 title_id) {
    switch (static_cast<u32>(title_id & 0xFFFFFFFF)) {
        case 0x00008602:  // Instruction Manual
        case 0x00009202:
        case 0x00009B02:
        case 0x0000A402:
        case 0x0000AC02:
        case 0x0000B402:
        case 0x00021A00:  // Garbage
        case 0x00022800:  // StreetPass Mii Plaza (manually excluded)
            return true;
    }
    u32 high = static_cast<u32>(title_id >> 32);
    if (high == 0x0004000E) return true;  // updates (no save of their own)
    if (high == 0x0004800F) return true;  // DSi non-executable data archives
    return false;
}

// TWL/DSiWare predicate transcribed data-only from Checkpoint
// 3ds/source/loader.cpp (scanInstalledTitles), fetched 2026-09-18:
//   const bool isTwl = ((id >> 44) & 0xF) == 8;
// (High32 0x00048xxx: DSiWare and system TWL titles on NAND.)
static bool is_twl_title(u64 title_id) {
    return ((title_id >> 44) & 0xF) == 8;
}

// Forward declarations: these helpers are defined next to open_save_archive
// below, but list_titles() calls them earlier in the file.
static Result open_twl_save_archive(FS_Archive* archive);
static std::string twl_save_root(u64 title_id);
static bool twl_save_accessible(FS_Archive archive, u64 title_id);
static void delete_dir_contents(FS_Archive archive, const std::string& dir);
static Result open_save_archive(u64 title_id, FS_Archive* archive);
static Result open_extdata_archive(u64 title_id, u32 extdata_id, FS_Archive* archive);
static void entry_name_ascii(const FS_DirectoryEntry& entry, char* out);

// Probe whether an already-opened archive root has at least one real entry (not . or ..).
// Closes the directory handle on all paths. Returns true if any real entry is found.
static bool probe_archive_has_entry(FS_Archive archive) {
    Handle dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, archive,
                 fsMakePath(PATH_ASCII, "/")))) {
        return false;
    }
    FS_DirectoryEntry entry;
    u32 n;
    bool found = false;
    while (!found) {
        n = 0;
        if (R_FAILED(FSDIR_Read(dir, &n, 1, &entry)) || n == 0) break;
        char ename[256];
        entry_name_ascii(entry, ename);
        if (strcmp(ename, ".") != 0 && strcmp(ename, "..") != 0) found = true;
    }
    FSDIR_Close(dir);
    return found;
}

// Light local-save probe: open the title's main save archive and/or extdata archive and
// check for at least one FS entry. Does NOT call extract_save_json (too heavy).
// Returns true if the title has any local save data worth syncing.
// For TWL titles the caller uses twl_save_accessible() instead (TWL FS already mounted).
static bool has_local_save(const TitleInfo& title) {
    FS_Archive archive;
    if (R_SUCCEEDED(open_save_archive(title.title_id, &archive))) {
        bool ok = probe_archive_has_entry(archive);
        FSUSER_CloseArchive(archive);
        if (ok) {
            printf("[titles] local save found (main) tid=%016llX\n",
                   (unsigned long long)title.title_id);
            return true;
        }
    }
    u32 ext_id = extdata_id_for(title.title_id);
    FS_Archive ext_archive;
    if (R_SUCCEEDED(open_extdata_archive(title.title_id, ext_id, &ext_archive))) {
        bool ok = probe_archive_has_entry(ext_archive);
        FSUSER_CloseArchive(ext_archive);
        if (ok) {
            printf("[titles] local save found (extdata) tid=%016llX extdata_id=0x%08lX\n",
                   (unsigned long long)title.title_id, (unsigned long)ext_id);
            return true;
        }
    }
    return false;
}

// Collapse per-game duplicates. The SD title list holds the base app
// (00040000...), its update (0004000E...) and any DLC (0004008C...) as
// separate entries, but they are ONE game sharing a unique_id. The savable
// entry is the base application: USER_SAVEDATA is keyed by its title_id and
// extdata by an id derived from its unique_id -- updates/DLC carry no save of
// their own. So per unique_id we keep the base app and drop the rest; when the
// base's own SMDH icon can't be read (icon empty) we borrow the name+icon from
// a sibling that has one, so the row still shows the real game.
static std::vector<TitleInfo> collapse_by_unique_id(const std::vector<TitleInfo>& raw) {
    std::vector<TitleInfo> out;
    std::vector<bool> used(raw.size(), false);
    for (size_t i = 0; i < raw.size(); i++) {
        if (used[i]) continue;
        u32 uid = raw[i].unique_id;
        int base_idx = -1, icon_idx = -1, group_n = 0;
        bool any_remote = false;
        for (size_t j = i; j < raw.size(); j++) {
            if (raw[j].unique_id != uid) continue;
            used[j] = true;
            group_n++;
            if (raw[j].has_remote) any_remote = true;
            if ((u32)(raw[j].title_id >> 32) == 0x00040000 && base_idx < 0) base_idx = (int)j;
            if (!raw[j].icon.empty() && icon_idx < 0) icon_idx = (int)j;
        }
        int canon = (base_idx >= 0) ? base_idx : (int)i;
        TitleInfo t = raw[canon];
        if (t.icon.empty() && icon_idx >= 0) {
            printf("[titles] uid=%05lX base tid=%016llX borrows name/icon from tid=%016llX\n",
                   (unsigned long)uid, (unsigned long long)t.title_id,
                   (unsigned long long)raw[icon_idx].title_id);
            t.name = raw[icon_idx].name;
            t.icon = raw[icon_idx].icon;
        }
        if (any_remote && !t.has_remote)
            printf("[titles] uid=%05lX has_remote inherited from a sibling entry\n",
                   (unsigned long)uid);
        t.has_remote = any_remote;
        if (group_n > 1)
            printf("[titles] uid=%05lX collapsed %d entries -> tid=%016llX '%s'\n",
                   (unsigned long)uid, group_n, (unsigned long long)t.title_id, t.name.c_str());
        out.push_back(t);
    }
    return out;
}

std::vector<TitleInfo> list_titles(const WsVault* vault, const WebDavCfg& dav) {
    // Fetch homebrew ID list from Universal-DB once per process so we can
    // hide homebrew/utility apps (Anemone3DS, Checkpoint, FBI, …) from the
    // backup list. Fail-open: if the fetch and SD cache both fail the set is
    // empty and all titles are shown.
    homebrew::ensure_loaded();

    // -- Remote backup set (single PROPFIND on the 3ds/ collection) --
    // filter_active=true even when remote set is empty (404); false only on transport error.
    std::set<std::string> remote_games;
    bool filter_active = vault && fetch_remote_game_set(vault, dav, "3ds", remote_games);

    std::vector<TitleInfo> sd_raw;
    std::vector<TitleInfo> nand_raw;

    if (R_FAILED(amInit())) {
        printf("[titles] amInit failed\n");
        return sd_raw;
    }

    // -- Source 1: SD titles --
    {
        u32 count = 0;
        Result count_rc = AM_GetTitleCount(MEDIATYPE_SD, &count);
        if (R_FAILED(count_rc)) {
            printf("[titles] AM_GetTitleCount(SD) failed rc=0x%08lX\n",
                   (unsigned long)count_rc);
        } else if (count > 0) {
            std::vector<u64> title_ids(count);
            u32 read = 0;
            Result list_rc = AM_GetTitleList(&read, MEDIATYPE_SD, count, title_ids.data());
            if (R_SUCCEEDED(list_rc)) {
                int icons_found = 0;
                int homebrew_hidden = 0;
                int filter_hidden = 0;
                int shown_via_remote = 0;
                for (u32 i = 0; i < read; i++) {
                    u64 tid = title_ids[i];
                    if (is_system_excluded(tid)) {
                        printf("[titles] sd tid=%016llX excluded (system)\n",
                               (unsigned long long)tid);
                        continue;
                    }
                    if (homebrew::is_homebrew(tid)) {
                        printf("[titles] hiding %s (tid=0x%016llX) as homebrew\n",
                               default_title_name(tid).c_str(),
                               (unsigned long long)tid);
                        homebrew_hidden++;
                        continue;
                    }

                    // Build TitleInfo and read SMDH BEFORE the filter so
                    // the game-key derivation uses the SMDH display name —
                    // the same name that extract_save_json / the sync engine will
                    // feed to the Rust normalizer.
                    TitleInfo info;
                    info.title_id  = tid;
                    info.unique_id = (tid >> 8) & 0xFFFFF;
                    info.is_twl    = false;
                    info.name      = default_title_name(info.title_id);
                    read_smdh(info.title_id, info.name, info.icon);

                    // Save-presence filter: show only titles with local save OR remote backup.
                    // Fail-open: if remote PROPFIND failed (filter_active==false), skip filter.
                    if (filter_active) {
                        bool in_remote = is_in_remote_set(vault, info.name, remote_games);
                        info.has_remote = in_remote;

                        if (!in_remote && !has_local_save(info)) {
                            printf("[titles] hiding %s (tid=0x%016llX) — no local save, no remote backup\n",
                                   info.name.c_str(), (unsigned long long)tid);
                            filter_hidden++;
                            continue;
                        }
                        if (in_remote) shown_via_remote++;
                    }

                    if (!info.icon.empty()) icons_found++;
                    sd_raw.push_back(info);
                }
                printf("[titles] SD: %zu titles (%d with icons, %d homebrew hidden)\n",
                       sd_raw.size(), icons_found, homebrew_hidden);
                if (filter_active) {
                    printf("[titles] SD save-presence filter: %d hidden, %d shown via remote backup\n",
                           filter_hidden, shown_via_remote);
                }
            } else {
                printf("[titles] AM_GetTitleList(SD) failed rc=0x%08lX\n",
                       (unsigned long)list_rc);
            }
        }
    }

    // -- Source 2: NAND TWL/DSiWare titles --
    // NAND lists hundreds of system titles; only TWL/DSiWare saves are in
    // scope. is_system_excluded + the TWL accessibility probe (mirrors
    // Checkpoint's SaveDataSource::accessible) keep noise rows out. TWL
    // titles have no SMDH, so no read_smdh — the hex-uid name stands.
    {
        u32 count = 0;
        Result count_rc = AM_GetTitleCount(MEDIATYPE_NAND, &count);
        if (R_FAILED(count_rc)) {
            printf("[titles] AM_GetTitleCount(NAND) failed rc=0x%08lX\n",
                   (unsigned long)count_rc);
        } else if (count > 0) {
            std::vector<u64> title_ids(count);
            u32 read = 0;
            Result list_rc = AM_GetTitleList(&read, MEDIATYPE_NAND, count, title_ids.data());
            if (R_SUCCEEDED(list_rc)) {
                // Mount the TWL NAND FAT ONCE for all probes (Checkpoint
                // mounts it a single time and checks each title inside).
                FS_Archive twl_archive;
                if (R_FAILED(open_twl_save_archive(&twl_archive))) {
                    printf("[titles] TWL archive unavailable — skipping NAND source\n");
                } else {
                    int nand_filter_hidden = 0;
                    int nand_shown_via_remote = 0;
                    for (u32 i = 0; i < read; i++) {
                        u64 tid = title_ids[i];
                        if (is_system_excluded(tid)) continue;
                        if (!is_twl_title(tid)) continue;

                        bool has_twl_local = twl_save_accessible(twl_archive, tid);
                        bool in_remote = true; // fail-open when the remote set is unavailable
                        if (filter_active) {
                            std::string gname = default_title_name(tid);
                            in_remote = is_in_remote_set(vault, gname, remote_games);

                            if (!has_twl_local && !in_remote) {
                                printf("[titles] hiding %s (tid=0x%016llX) — no TWL save, no remote backup\n",
                                       gname.c_str(), (unsigned long long)tid);
                                nand_filter_hidden++;
                                continue;
                            }
                            if (in_remote) nand_shown_via_remote++;
                        } else if (!has_twl_local) {
                            // Fail-open: revert to original twl_save_accessible filter
                            continue;
                        }

                        TitleInfo info;
                        info.title_id = tid;
                        info.unique_id = (tid >> 8) & 0xFFFFF;
                        info.is_twl = true;
                        info.name = default_title_name(tid);
                        info.has_remote = in_remote;
                        nand_raw.push_back(info);
                    }
                    FSUSER_CloseArchive(twl_archive);
                    if (filter_active && (nand_filter_hidden || nand_shown_via_remote)) {
                        printf("[titles] NAND TWL save-presence filter: %d hidden, %d via remote\n",
                               nand_filter_hidden, nand_shown_via_remote);
                    }
                }
                printf("[titles] NAND TWL: %zu kept of %lu listed\n",
                       nand_raw.size(), (unsigned long)read);
            } else {
                printf("[titles] AM_GetTitleList(NAND) failed rc=0x%08lX\n",
                       (unsigned long)list_rc);
            }
        }
    }

    amExit();

    // Collapse SD per unique_id (base app vs update/DLC sharing one save).
    // TWL NAND titles are singleton groups — a TWL title has no update/DLC
    // siblings — so they are appended without collapsing.
    std::vector<TitleInfo> out = collapse_by_unique_id(sd_raw);
    out.insert(out.end(), nand_raw.begin(), nand_raw.end());

    // -- Source 3: TWiLight Menu++ ROM saves (ROM-driven; one PROPFIND per system with ROMs) --
    {
        std::vector<TitleInfo> roms = scan_rom_titles();
        std::map<std::string, std::set<std::string> > rom_remote;
        std::map<std::string, bool> rom_remote_ok;
        int rom_hidden = 0, rom_kept = 0;
        for (size_t i = 0; i < roms.size(); i++) {
            TitleInfo& r = roms[i];
            bool active = false;
            if (vault) {
                if (!rom_remote_ok.count(r.system))
                    rom_remote_ok[r.system] =
                        fetch_remote_game_set(vault, dav, r.system.c_str(), rom_remote[r.system]);
                active = rom_remote_ok[r.system];
            }
            if (active) {
                r.has_remote = is_key_in_remote_set(vault, r.rom_id, rom_remote[r.system]);
                if (!r.has_remote && r.save_paths.empty()) {
                    printf("[titles] hiding ROM %s — no local save, no remote backup\n",
                           r.rom_file_name.c_str());
                    rom_hidden++;
                    continue;
                }
            }
            out.push_back(r);
            rom_kept++;
        }
        printf("[titles] ROM source: %d kept, %d hidden (of %zu scanned)\n",
               rom_kept, rom_hidden, roms.size());
    }

    std::stable_sort(out.begin(), out.end(),
                     [](const TitleInfo& a, const TitleInfo& b) { return title_name_less(a.name, b.name); });
    size_t no_remote = 0;
    for (size_t i = 0; i < out.size(); i++)
        if (!out[i].has_remote) no_remote++;
    printf("[titles] %zu title(s) after save-presence filter (sd + nand twl), %zu without remote backup%s\n",
           out.size(), no_remote,
           filter_active ? "" : " (remote set unavailable: all marked has_remote, fail-open)");
    return out;
}

// Open the ARCHIVE_USER_SAVEDATA for a given title_id on SD media.
// PATH_BINARY layout {mediatype, lowid, highid} verified byte-identical
// against Checkpoint 3ds/source/archive.cpp Archive::save (non-NAND branch).
// Caller must FSUSER_CloseArchive on success.
static Result open_save_archive(u64 title_id, FS_Archive* archive) {
    u32 path_data[3] = {static_cast<u32>(MEDIATYPE_SD),
                        static_cast<u32>(title_id & 0xFFFFFFFF),
                        static_cast<u32>(title_id >> 32)};
    FS_Path archive_path = {PATH_BINARY, sizeof(path_data), path_data};
    Result rc = FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, archive_path);
    if (R_FAILED(rc)) {
        printf("[saves] open_save_archive FAIL tid=%016llX rc=0x%08lX\n",
               (unsigned long long)title_id, (unsigned long)rc);
    }
    return rc;
}

// Open the ARCHIVE_NAND_TWL_FS (whole TWL NAND FAT). Transcribed data-only
// from Checkpoint 3ds/source/archive.cpp (SaveDataSource::open, Kind::TwlSave):
// the archive opens with PATH_EMPTY; the per-title save lives at a path
// inside it (see twl_save_root). Caller must FSUSER_CloseArchive on success.
static Result open_twl_save_archive(FS_Archive* archive) {
    Result rc = FSUSER_OpenArchive(archive, ARCHIVE_NAND_TWL_FS,
                                   fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(rc)) {
        printf("[saves] open_twl_save_archive FAIL rc=0x%08lX\n", (unsigned long)rc);
    }
    return rc;
}

// Per-title save root inside ARCHIVE_NAND_TWL_FS, WITHOUT leading slash
// (e.g. "title/00030004/000012AB/data"). Transcribed data-only from
// Checkpoint 3ds/source/archive.cpp (Archive::twlSaveDataPath): TWLN stores
// titles under 000300xx even though AM reports them as 00048xxx, so the
// high id is normalized via (high & 0x00000FFF) | 0x00030000.
static std::string twl_save_root(u64 title_id) {
    u32 high = static_cast<u32>(title_id >> 32);
    u32 low  = static_cast<u32>(title_id & 0xFFFFFFFF);
    char buf[64];
    snprintf(buf, sizeof(buf), "title/%08lX/%08lX/data",
             (unsigned long)((high & 0x00000FFF) | 0x00030000),
             (unsigned long)low);
    return std::string(buf);
}

// Accessibility probe for TWL/DSiWare titles, mirroring Checkpoint's
// SaveDataSource::accessible (archive.cpp): a TWL title is listable only
// if its per-title data directory inside ARCHIVE_NAND_TWL_FS exists.
// Without this, the NAND enumeration would surface TWL titles whose saves
// do not exist. `archive` is the already-mounted TWL NAND FAT (mounted
// once by the caller for the whole enumeration).
static bool twl_save_accessible(FS_Archive archive, u64 title_id) {
    Handle dir;
    std::string path = "/" + twl_save_root(title_id);
    bool ok = R_SUCCEEDED(FSUSER_OpenDirectory(&dir, archive,
                          fsMakePath(PATH_ASCII, path.c_str())));
    if (ok) FSDIR_Close(dir);
    printf("[titles] twl probe tid=%016llX accessible=%d\n",
           (unsigned long long)title_id, (int)ok);
    return ok;
}

// Convert a directory entry's UTF-16 name to a NUL-terminated ASCII C
// string in `out` (>= 256 bytes). Non-ASCII code units are dropped —
// sufficient for save file names.
static void entry_name_ascii(const FS_DirectoryEntry& entry, char* out) {
    int j = 0;
    for (int k = 0; k < 256 && entry.name[k] != 0; k++) {
        if (entry.name[k] < 128)
            out[j++] = static_cast<char>(entry.name[k]);
    }
    out[j] = '\0';
}

// Delete the CONTENTS of `dir` (a path relative to the archive root, no
// leading slash) but keep `dir` itself. Two passes — collect the listing,
// close the directory, then delete — so we never delete entries out from
// under an open FSDIR_Read cursor. Mirrors Checkpoint io::restore's TWL
// branch ("the TWL FAT `data` directory must survive; only its contents
// go"), which uses deleteFolderContentsRecursively instead of
// FSUSER_DeleteDirectoryRecursively.
static void delete_dir_contents(FS_Archive archive, const std::string& dir) {
    Handle dh;
    std::string open_path = "/" + dir;
    if (R_FAILED(FSUSER_OpenDirectory(&dh, archive,
                 fsMakePath(PATH_ASCII, open_path.c_str())))) {
        printf("[saves] delete_dir_contents: open failed %s (non-fatal)\n",
               open_path.c_str());
        return;
    }
    std::vector<std::pair<std::string, bool>> children; // (name, is_dir)
    FS_DirectoryEntry entry;
    u32 n = 0;
    while (R_SUCCEEDED(FSDIR_Read(dh, &n, 1, &entry)) && n > 0) {
        char name[256];
        entry_name_ascii(entry, name);
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        children.push_back({name, (entry.attributes & FS_ATTRIBUTE_DIRECTORY) != 0});
    }
    FSDIR_Close(dh);
    for (const auto& child : children) {
        std::string child_path = "/" + dir + "/" + child.first;
        Result rc;
        if (child.second) {
            delete_dir_contents(archive, dir + "/" + child.first);
            rc = FSUSER_DeleteDirectory(archive,
                     fsMakePath(PATH_ASCII, child_path.c_str()));
        } else {
            rc = FSUSER_DeleteFile(archive,
                     fsMakePath(PATH_ASCII, child_path.c_str()));
        }
        if (R_FAILED(rc)) {
            printf("[saves] delete_dir_contents: delete failed %s rc=0x%08lX (non-fatal)\n",
                   child_path.c_str(), (unsigned long)rc);
        }
    }
    printf("[saves] delete_dir_contents %s: %zu entr(ies) removed\n",
           open_path.c_str(), children.size());
}

// Open the ARCHIVE_EXTDATA for a given extdata_id (SD card).
// Caller must FSUSER_CloseArchive on success.
// PATH_BINARY layout VERIFIED against 3dbrew "Extdata" + Checkpoint io.cpp:
//   {mediatype=MEDIATYPE_SD, extdata_id_low, extdata_id_high=0}
// 3dbrew documents the extdata archive path as a 12-byte binary:
//   u32 mediaType | u32 extdataIdLow | u32 extdataIdHigh
// Checkpoint opens extdata the same way in its ArchiveHandle for BackupKind::Extdata.
static Result open_extdata_archive(u64 title_id, u32 extdata_id,
                                   FS_Archive* archive) {
    u32 path_data[3] = { MEDIATYPE_SD, extdata_id, 0x00000000 };
    FS_Path archive_path = {PATH_BINARY, sizeof(path_data), path_data};
    Result rc = FSUSER_OpenArchive(archive, ARCHIVE_EXTDATA, archive_path);
    if (R_SUCCEEDED(rc)) {
        printf("[saves] open_extdata_archive OK tid=%016llX extdata_id=0x%08lX\n",
               (unsigned long long)title_id, (unsigned long)extdata_id);
    } else {
        printf("[saves] open_extdata_archive FAIL tid=%016llX extdata_id=0x%08lX rc=0x%08lX\n",
               (unsigned long long)title_id, (unsigned long)extdata_id, (unsigned long)rc);
    }
    return rc;
}

// Recursively walk a 3DS save archive directory using libctru FS API.
static void walk_archive(FS_Archive archive, const char* rel,
                         std::vector<std::pair<std::string, std::vector<uint8_t>>>* out) {
    Handle dir;
    std::string dir_path = std::string("/") + rel;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, archive,
                 fsMakePath(PATH_ASCII, dir_path.c_str())))) {
        return;
    }

    FS_DirectoryEntry entry;
    u32 entries_read = 0;

    while (true) {
        entries_read = 0;
        if (R_FAILED(FSDIR_Read(dir, &entries_read, 1, &entry)) || entries_read == 0)
            break;

        // Convert UTF-16 name to ASCII (sufficient for save file names)
        char name[256];
        entry_name_ascii(entry, name);

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        std::string child_rel;
        if (rel[0] != '\0')
            child_rel = std::string(rel) + "/" + name;
        else
            child_rel = name;

        if (entry.attributes & FS_ATTRIBUTE_DIRECTORY) {
            walk_archive(archive, child_rel.c_str(), out);
        } else {
            Handle file;
            std::string file_path = std::string("/") + child_rel;
            if (R_FAILED(FSUSER_OpenFile(&file, archive,
                         fsMakePath(PATH_ASCII, file_path.c_str()),
                         FS_OPEN_READ, 0))) {
                continue;
            }

            u64 file_size = 0;
            FSFILE_GetSize(file, &file_size);

            std::vector<uint8_t> content(static_cast<size_t>(file_size));
            if (file_size > 0) {
                u32 bytes_read = 0;
                if (R_FAILED(FSFILE_Read(file, &bytes_read, 0,
                             content.data(), static_cast<u32>(file_size)))) {
                    FSFILE_Close(file);
                    continue;
                }
                if (bytes_read != static_cast<u32>(file_size)) {
                    FSFILE_Close(file);
                    continue;
                }
            }
            FSFILE_Close(file);
            out->push_back({child_rel, content});
        }
    }

    FSDIR_Close(dir);
}

std::vector<uint8_t> extract_save_json(const TitleInfo& title) {
    if (title.source == SourceRom) return extract_rom_saves(title);
    // -- Phase 1: user/TWL savedata (routed by title origin) --
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    {
        FS_Archive archive;
        bool opened = false;
        std::string walk_root; // "" for user saves; per-title path for TWL
        const char* kind_name;
        if (title.is_twl) {
            kind_name = "TWL";
            opened = R_SUCCEEDED(open_twl_save_archive(&archive));
            walk_root = twl_save_root(title.title_id);
        } else {
            kind_name = "USER_SAVEDATA";
            opened = R_SUCCEEDED(open_save_archive(title.title_id, &archive));
        }
        if (opened) {
            walk_archive(archive, walk_root.c_str(), &files);
            FSUSER_CloseArchive(archive);
            if (title.is_twl) {
                // Strip the per-title root prefix: write_save_files
                // re-prepends twl_save_root() on restore, so stored paths
                // must be relative to the data dir — otherwise the
                // extract->restore round-trip doubles the prefix and files
                // land in a nested, never-read directory.
                const std::string prefix = walk_root + "/";
                for (auto& f : files) {
                    if (f.first.compare(0, prefix.size(), prefix) == 0)
                        f.first.erase(0, prefix.size());
                }
            }
            printf("[saves] extract %s tid=%016llX files=%zu\n",
                   kind_name, (unsigned long long)title.title_id, files.size());
        } else {
            printf("[saves] extract %s open failed tid=%016llX (skipping)\n",
                   kind_name, (unsigned long long)title.title_id);
        }
    }
    // -- Phase 2: EXTDATA --
    std::vector<std::pair<std::string, std::vector<uint8_t>>> extdata_files;
    {
        u32 ext_id = extdata_id_for(title.title_id);
        FS_Archive ext_archive;
        if (R_SUCCEEDED(open_extdata_archive(title.title_id, ext_id, &ext_archive))) {
            walk_archive(ext_archive, "", &extdata_files);
            FSUSER_CloseArchive(ext_archive);
            printf("[saves] extract EXTDATA tid=%016llX extdata_id=0x%08lX files=%zu\n",
                   (unsigned long long)title.title_id, (unsigned long)ext_id,
                   extdata_files.size());
        } else {
            printf("[saves] extract EXTDATA open failed tid=%016llX extdata_id=0x%08lX (no extdata, skipping)\n",
                   (unsigned long long)title.title_id, (unsigned long)ext_id);
        }
    }
    if (files.empty() && extdata_files.empty()) return std::vector<uint8_t>();

    char hex_uid[8];
    snprintf(hex_uid, sizeof(hex_uid), "%05X", (unsigned)title.unique_id);
    std::string checkpoint_dir = std::string("0x") + hex_uid + " " + title.name;

    std::vector<std::pair<std::string, std::vector<uint8_t>>> wrapped;
    wrapped.reserve(files.size() + extdata_files.size());
    for (auto& f : files) {
        wrapped.push_back({checkpoint_dir + "/main/" + f.first, std::move(f.second)});
    }
    for (auto& f : extdata_files) {
        wrapped.push_back({checkpoint_dir + "/extdata/" + f.first, std::move(f.second)});
    }
    std::vector<uint8_t> tree = file_tree_encode(wrapped);
    printf("[saves] extract: encoded file_tree %zu bytes (%zu files)\n",
           tree.size(), wrapped.size());
    return tree;
}

std::string current_utc_time() {
    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return buf;
}

std::string get_device_id() {
    const char* path = "sdmc:/waystone/device_id.txt";
    const char* dir = "sdmc:/waystone";

    FILE* f = fopen(path, "r");
    if (f) {
        char buf[65] = {};
        size_t n = fread(buf, 1, 64, f);
        fclose(f);
        if (n >= 16) return std::string(buf, n);
    }

    // Generate new random ID (32 hex chars = 16 random bytes).
    // psInit() must have been called in main() before this point.
    uint8_t rand_bytes[16];
    PS_GenerateRandomBytes(rand_bytes, sizeof(rand_bytes));
    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", rand_bytes[i]);
    }

    mkdir(dir, 0755);
    f = fopen(path, "w");
    if (f) {
        fwrite(hex, 1, 32, f);
        fclose(f);
    }

    return std::string(hex, 32);
}

int write_save_files(const TitleInfo& title, const uint8_t* ft_ptr, size_t ft_len,
                     SaveArchiveKind kind) {
    std::vector<FileTreeEntry> entries;
    if (!file_tree_decode(ft_ptr, ft_len, &entries)) {
        printf("[saves] write_save_files: file_tree_decode failed (len=%zu)\n", ft_len);
        return -1;
    }
    printf("[saves] write_save_files: decoded %zu files (%zu bytes)\n",
           entries.size(), ft_len);
    if ((title.source == SourceRom) != (kind == SaveRomFile)) {
        printf("[saves] write_save_files: source/kind mismatch (source=%d kind=%s) — refusing\n",
               (int)title.source, save_kind_name(kind));
        return -1;
    }
    if (kind == SaveRomFile) return write_rom_saves(title, entries);
    const u64 title_id = title.title_id;
    FS_Archive archive;
    // Path prefix inside the archive for TWL saves ("" for all other kinds).
    std::string root_prefix;
    if (kind == SaveExtdata) {
        u32 ext_id = extdata_id_for(title_id);
        if (R_FAILED(open_extdata_archive(title_id, ext_id, &archive))) {
            printf("[saves] write_save_files: open_extdata_archive failed tid=%016llX\n",
                   (unsigned long long)title_id);
            return -1;
        }
    } else if (kind == SaveTwl) {
        if (R_FAILED(open_twl_save_archive(&archive))) {
            printf("[saves] write_save_files: open_twl_save_archive failed tid=%016llX\n",
                   (unsigned long long)title_id);
            return -1;
        }
        root_prefix = twl_save_root(title_id);
    } else {
        if (R_FAILED(open_save_archive(title_id, &archive))) {
            printf("[saves] write_save_files: open_save_archive failed tid=%016llX\n",
                   (unsigned long long)title_id);
            return -1;
        }
    }
    printf("[saves] write_save_files: opened archive kind=%s tid=%016llX\n",
           save_kind_name(kind), (unsigned long long)title_id);

    if (kind == SaveUser) {
        // Wipe archive root before writing so stale files from a previous
        // backup do not survive. Non-fatal: an empty/fresh archive may return
        // an error. Mirrors Checkpoint io::restore (FSUSER_DeleteDirectoryRecursively
        // for non-TWL, non-extdata saves).
        Result del_res = FSUSER_DeleteDirectoryRecursively(archive,
                             fsMakePath(PATH_ASCII, "/"));
        if (R_FAILED(del_res)) {
            printf("[saves] write_save_files: DeleteDirRecursively user 0x%08lX (non-fatal)\n",
                   (unsigned long)del_res);
        }
    } else if (kind == SaveTwl) {
        // TWL: wipe ONLY the contents of the per-title data directory; the
        // directory itself must survive (Checkpoint io::restore isTwl branch
        // uses deleteFolderContentsRecursively, NOT DeleteDirectoryRecursively).
        delete_dir_contents(archive, root_prefix);
    } else {
        // DIVERGENCE (extdata root wipe): Checkpoint io::restore calls
        // deleteFolderRecursively(archive, "/") for extdata — a custom
        // recursive function that deletes all files and subdirs inside "/"
        // then tries FSUSER_DeleteDirectory("/") (which fails on root,
        // ignored). Net effect: all contents of "/" are deleted; the root
        // directory itself survives. FSUSER_DeleteDirectoryRecursively on "/"
        // has the same observable effect (wipes contents, root cannot be
        // removed). We mirror Checkpoint: wipe extdata archive contents
        // before restore. Hardware verify needed: if FSUSER_DeleteDirectoryRecursively
        // on extdata root returns a hard failure, change to a manual
        // walk-and-delete using walk_archive-style iteration.
        Result del_res = FSUSER_DeleteDirectoryRecursively(archive,
                             fsMakePath(PATH_ASCII, "/"));
        if (R_FAILED(del_res)) {
            printf("[saves] write_save_files: extdata root wipe 0x%08lX (non-fatal, mirrors Checkpoint)\n",
                   (unsigned long)del_res);
        } else {
            printf("[saves] write_save_files: extdata root wiped (mirrors Checkpoint)\n");
        }
    }

    int ret = 0;

    for (size_t e = 0; e < entries.size(); e++) {
        std::string path = entries[e].first;
        const std::vector<uint8_t>& bytes = entries[e].second;

        if (path.empty()) {
            printf("[saves] write_save_files: missing path in file entry\n");
            ret = -1;
            continue;
        }

        // TWL saves live under a per-title directory inside ARCHIVE_NAND_TWL_FS;
        // all other kinds write at the archive root.
        if (!root_prefix.empty()) {
            path = root_prefix + "/" + path;
        }

        // Create parent directories under the archive root.
        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos && last_slash > 0) {
            std::string dir_part = path.substr(0, last_slash);
            std::string accumulated;
            size_t start = 0;
            while (start < dir_part.size()) {
                size_t end = dir_part.find('/', start);
                if (end == std::string::npos) end = dir_part.size();
                if (end > start) {
                    accumulated += "/" + dir_part.substr(start, end - start);
                    FSUSER_CreateDirectory(archive,
                        fsMakePath(PATH_ASCII, accumulated.c_str()), 0);
                    // ignore result — already-exists is expected and OK
                }
                start = end + 1;
            }
        }

        // Open (or create) the file for writing.
        Handle fh;
        std::string file_path = "/" + path;
        if (R_FAILED(FSUSER_OpenFile(&fh, archive,
                     fsMakePath(PATH_ASCII, file_path.c_str()),
                     FS_OPEN_CREATE | FS_OPEN_WRITE, 0))) {
            printf("[saves] write_save_files: FSUSER_OpenFile failed for %s\n", path.c_str());
            ret = -1;
            continue;
        }

        // Truncate to exact size (FS_OPEN_WRITE does not truncate unlike fopen "wb").
        if (R_FAILED(FSFILE_SetSize(fh, static_cast<u64>(bytes.size())))) {
            printf("[saves] write_save_files: FSFILE_SetSize failed for %s\n", path.c_str());
            FSFILE_Close(fh);
            ret = -1;
            continue;
        }

        if (!bytes.empty()) {
            u32 written = 0;
            if (R_FAILED(FSFILE_Write(fh, &written, 0,
                         bytes.data(), static_cast<u32>(bytes.size()),
                         FS_WRITE_FLUSH))) {
                printf("[saves] write_save_files: FSFILE_Write failed for %s\n", path.c_str());
                FSFILE_Close(fh);
                ret = -1;
                continue;
            }
        }
        FSFILE_Close(fh);
    }

    // Do NOT commit a partial restore — a half-written committed save is worse
    // than a failed restore (mirrors Checkpoint aborting on first copy failure).
    if (ret != 0) {
        FSUSER_CloseArchive(archive);
        return -1;
    }

    if (kind == SaveUser) {
        // Flush all pending writes to the underlying save filesystem.
        // Mirrors Checkpoint io::restore: commit only when
        // 'kind == Save && !isTwl' (i.e. never for TWL or extdata).
        if (R_FAILED(FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA,
                                           NULL, 0, NULL, 0))) {
            printf("[saves] write_save_files: COMMIT_SAVE_DATA failed (kind=%s)\n",
                   save_kind_name(kind));
            FSUSER_CloseArchive(archive);
            return -1;
        }
        printf("[saves] write_save_files: committed save tid=%016llX kind=%s\n",
               (unsigned long long)title_id, save_kind_name(kind));
    } else {
        // SaveTwl: a TWL FAT write needs no commit (Checkpoint io::restore).
        // SaveExtdata: files written to ARCHIVE_EXTDATA are immediately
        // visible without a commit step (existing divergence comment).
        printf("[saves] write_save_files: commit skipped kind=%s (mirrors Checkpoint)\n",
               save_kind_name(kind));
    }
    FSUSER_CloseArchive(archive);

    if (kind == SaveUser) {
        // Delete the console secure value so the game regenerates a fresh one
        // instead of rejecting the restored save. Mirrors Checkpoint's
        // 'kind == Save && !isTwl' block in io::restore — Checkpoint uses
        // SECUREVALUE_SLOT_SD with (uniqueId << 8) == (low & 0xFFFFFF00).
        // Use the RAW low id (title_id & 0xFFFFFF00ULL), NOT the 20-bit
        // unique_id. Non-fatal.
        u8 sv_out = 0;
        u64 secure_value = ((u64)SECUREVALUE_SLOT_SD << 32) | (u64)(title_id & 0xFFFFFF00ULL);
        Result sv = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE,
                        &secure_value, sizeof(secure_value),
                        &sv_out, sizeof(sv_out));
        if (R_FAILED(sv)) {
            printf("[saves] write_save_files: ControlSecureSave delete 0x%08lX (non-fatal)\n",
                   (unsigned long)sv);
        }
    } else {
        // SaveTwl: TWL saves have no console secure value (Checkpoint
        // !isTwl guard). SaveExtdata: no secure value either.
        printf("[saves] write_save_files: secure-value delete SKIPPED kind=%s\n",
               save_kind_name(kind));
    }

    return 0;
}
