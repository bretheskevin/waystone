#ifndef WAYSTONE_3DS_SAVES_H
#define WAYSTONE_3DS_SAVES_H

#include <cstdint>
#include <string>
#include <vector>
#include <3ds.h>
#include "net.h"

struct Vault;
typedef Vault WsVault;

enum TitleSource { SourceArchive = 0, SourceRom = 1 };

struct TitleInfo {
    u64 title_id;
    u32 unique_id; // (title_id >> 8) & 0xFFFFF
    bool is_twl;   // DSiWare on NAND (TWL). Predicate per Checkpoint: (title_id >> 44) & 0xF == 8
    std::string name;
    std::vector<uint8_t> icon; // SMDH 4608 B RGB565 tiled 48x48 (archives) | ROMs: RGBA5551 linear, 2048 B 32x32 NDS banner or 4608 B 48x48 TWiLight box art (rom_icon_side) | empty
    bool has_remote = true; // remote game dir seen by list_titles' PROPFIND; true when unknown (fail-open)
    TitleSource source = SourceArchive;
    std::string system;                  // ROM system seg ("nds","gba",...); empty for archives
    std::string rom_id;                  // core ROM identity (group_key game segment)
    std::string rom_path;                // absolute "sdmc:/roms/..." ROM path
    std::string rom_file_name;           // ROM file name, drives native save names on restore
    std::string save_dir;                // absolute save dir; restore target when no save exists yet
    std::vector<std::string> save_paths; // absolute paths of the ROM's existing saves
};

// Enumerate installed titles via AM service (SD + NAND TWL/DSiWare).
std::vector<TitleInfo> list_titles(const WsVault* vault, const WebDavCfg& dav);

// Extract savedata for a title as a WsFileTree buffer (binary file-tree).
// Empty vector = failure / no save.
// Paths formatted for ws_checkpoint_normalize:
//   "0x<5-hex uniqueID> <name>/main/<relative_file_path>"   (USER_SAVEDATA)
//   "0x<5-hex uniqueID> <name>/extdata/<relative_file_path>" (EXTDATA, if present)
// No uid parameter -- 3DS savedata is per-title, not per-user.
std::vector<uint8_t> extract_save_json(const TitleInfo& title);

// Get current UTC time as ISO 8601 string.
std::string current_utc_time();

// Get or create a persistent device ID (stored at sdmc:/waystone/device_id.txt).
std::string get_device_id();

// Read a file into a newly malloc'd buffer.
// Returns the buffer (caller owns → free()) and sets *len_out, or nullptr/0 on
// missing/empty/short-read.
uint8_t* read_keys_file(const char* path, long* len_out);

enum SaveArchiveKind {
    SaveUser,    // ARCHIVE_USER_SAVEDATA (SD media)
    SaveExtdata, // ARCHIVE_EXTDATA (always on SD)
    SaveTwl,     // ARCHIVE_NAND_TWL_FS, per-title /title/.../data root (DSiWare)
    SaveRomFile  // TWiLight ROM save files via stdio in TitleInfo.save_dir
};

// Human-readable kind name for log lines ("user" / "extdata" / "twl").
inline const char* save_kind_name(SaveArchiveKind kind) {
    switch (kind) {
        case SaveUser:    return "user";
        case SaveExtdata: return "extdata";
        case SaveTwl:     return "twl";
        case SaveRomFile: return "rom";
    }
    return "unknown";
}

// The archive kind for a title's "main" save slot, derived from its origin.
// TWL -> SaveTwl; SD -> SaveUser.
SaveArchiveKind main_save_kind(const TitleInfo& title);

// Restore a WsFileTree buffer (from ws_unzip or snapshot_to_file_tree) into the title's save archive, then commit (commit/secure-value skipped for
// SaveTwl and SaveExtdata, mirroring Checkpoint's `kind == Save && !isTwl` guard).
// kind is mandatory: for the "main" slot callers must route via main_save_kind()
// so TWL titles never silently take the SaveUser path.
// Returns 0 on success, -1 on mount/write/commit failure.
int write_save_files(const TitleInfo& title, const uint8_t* ft_ptr, size_t ft_len,
                     SaveArchiveKind kind);

// Return the extdata archive ID for a given title_id.
// Uses a quirks table (factual data from Checkpoint reference) for known
// first-party titles whose extdata ID differs from the default (low>>8).
u32 extdata_id_for(u64 title_id);

#endif
