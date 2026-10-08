#include "sync.h"
#include "json.h"
#include "file_tree.h"
#include "sync_rules.h"
#include "sync_summary.h"  // href_last_segment
#include "rom_parse.h"

#include <3ds.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>

extern "C" {
#include "waystone.h"
}

static const char* ffi_err() { return ws_last_error() ? ws_last_error() : "unknown"; }

static WsBuf normalize_title(const TitleInfo& title, const std::vector<uint8_t>& raw) {
    if (title.source == SourceRom) {
        printf("[sync] normalize rom system=%s rom_id=%s file=%s\n", title.system.c_str(),
               title.rom_id.c_str(), title.rom_file_name.c_str());
        return ws_rom_keyed_normalize(title.system.c_str(), title.rom_id.c_str(), title.name.c_str(),
                                      title.rom_file_name.c_str(), raw.data(), raw.size());
    }
    return ws_checkpoint_normalize("3ds", raw.data(), raw.size());
}

// Archive saves (user/extdata/TWL) expose no per-file mtime through FSUSER -> "" (NewestWins
// then treats the save as Prompt). ROM saves are plain SD files: newest stat() mtime.
static std::string ctr_local_mtime(const TitleInfo& title) {
    if (title.source != SourceRom) {
        printf("[sync] %s: archive save has no file mtime -> local mtime unknown\n", title.name.c_str());
        return "";
    }
    long long newest = 0;
    for (size_t i = 0; i < title.save_paths.size(); i++) {
        struct stat st;
        if (stat(title.save_paths[i].c_str(), &st) != 0) {
            printf("[sync] stat %s failed\n", title.save_paths[i].c_str());
            continue;
        }
        if ((long long)st.st_mtime > newest) newest = (long long)st.st_mtime;
    }
    std::string iso = plausible_mtime_iso(newest, (long long)time(NULL));
    printf("[sync] %s: rom save mtime=%lld -> '%s'\n", title.rom_file_name.c_str(), newest,
           iso.empty() ? "(unusable)" : iso.c_str());
    return iso;
}

static int ctr_list_saves(void*, const void* tp, LocalSaveSet& out) {
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    std::vector<uint8_t> raw = extract_save_json(title);
    if (raw.empty()) {
        printf("[saves] %s: no local save data\n", title.name.c_str());
        return 0;
    }
    size_t raw_len = raw.size();
    WsBuf sl = normalize_title(title, raw);
    if (!sl.ptr) {
        printf("[sync] %s: normalize failed (%s)\n", title.name.c_str(), ffi_err());
        return -1;
    }
    size_t sl_len = sl.len;
    if (!out.adopt_normalized(sl.ptr, sl.len, raw, ctr_local_mtime(title))) {
        printf("[sync] %s: save list decode failed (%zu bytes)\n", title.name.c_str(), sl_len);
        return -1;
    }
    printf("[sync] %s: normalize ok (raw %zu -> %zu bytes, %zu saves)\n", title.name.c_str(),
           raw_len, sl_len, out.saves.size());
    return 0;
}

// ROM saves that exist on the server but not on this SD yet (port of
// append_remote_only_rom_decisions; gated on SourceRom && has_remote).
static int ctr_list_remote_only(void*, const WsVault* vault, WebDavSession* dav, const void* tp,
                                const std::vector<std::string>& local_gks,
                                std::vector<std::string>& out) {
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    if (title.source != SourceRom || !title.has_remote) return 0;
    const std::string game_key = rom_group_key(title.system, title.rom_id, 0);
    char* slots_c = ws_rom_keyed_slots(title.system.c_str());
    if (!slots_c) {
        printf("[sync] remote-only %s: ws_rom_keyed_slots failed: %s\n", game_key.c_str(), ffi_err());
        return -1;
    }
    std::string slots(slots_c);
    ws_string_free(slots_c);

    std::string probe = sync_base_path(vault, rom_group_key(title.system, title.rom_id, "battery"));
    if (probe.empty()) return -1;
    std::string game_dir = probe.substr(0, probe.rfind('/'));
    std::vector<std::string> hrefs;
    u64 t0 = osGetTime();
    int rc = webdav_propfind_s(dav, game_dir.c_str(), &hrefs);
    if (rc != 0) {
        printf("[sync] remote-only %s: PROPFIND failed rc=%d\n", game_key.c_str(), rc);
        return -1;
    }
    std::set<std::string> remote_slots;
    for (size_t i = 0; i < hrefs.size(); i++) remote_slots.insert(href_last_segment(hrefs[i]));
    printf("[sync] remote-only %s: %zu remote entr%s in %llu ms\n", game_key.c_str(), hrefs.size(),
           hrefs.size() == 1 ? "y" : "ies", (unsigned long long)(osGetTime() - t0));

    size_t start = 0;
    while (start < slots.size()) {
        size_t nl = slots.find('\n', start);
        if (nl == std::string::npos) nl = slots.size();
        std::string slot = slots.substr(start, nl - start);
        start = nl + 1;
        if (slot.empty()) continue;
        std::string gk = rom_group_key(title.system, title.rom_id, slot.c_str());
        bool local = false;
        for (size_t i = 0; i < local_gks.size(); i++)
            if (local_gks[i] == gk) { local = true; break; }
        if (local) continue;
        std::string base = sync_base_path(vault, gk);
        if (base.empty() || !remote_slots.count(base.substr(base.rfind('/') + 1))) continue;
        printf("[sync] remote-only %s: on server, not on SD -> restore candidate\n", gk.c_str());
        out.push_back(gk);
    }
    return 0;
}

static int ctr_write_save(void*, const void* tp, const std::string& group_key,
                          const uint8_t* tree, size_t tree_len) {
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    std::string slot;
    size_t last_slash = group_key.rfind('/');
    if (last_slash != std::string::npos) slot = group_key.substr(last_slash + 1);

    if (title.source == SourceRom) {
        WsBuf native = {nullptr, 0};
        int nrc = ws_rom_keyed_to_native(title.system.c_str(), slot.c_str(),
                                         title.rom_file_name.c_str(), tree, tree_len, &native);
        if (nrc != 0) {
            printf("[sync] ws_rom_keyed_to_native failed for %s: %s\n", group_key.c_str(), ffi_err());
            return -1;
        }
        printf("[saves] rom restore %s -> dir=%s (%zu bytes native tree)\n", group_key.c_str(),
               title.save_dir.c_str(), (size_t)native.len);
        int rrc = write_save_files(title, native.ptr, native.len, SaveRomFile);
        ws_buf_free(native);
        printf("[saves] rom restore %s rc=%d\n", group_key.c_str(), rrc);
        return rrc;
    }

    SaveArchiveKind kind = (slot == "extdata") ? SaveExtdata : main_save_kind(title);
    printf("[saves] restore routing group_key=%s -> kind=%s\n", group_key.c_str(), save_kind_name(kind));
    int wrc = write_save_files(title, tree, tree_len, kind);
    printf("[saves] restore %s rc=%d\n", group_key.c_str(), wrc);
    return wrc;
}

const ShellOps& ctr_shell_ops() {
    static const ShellOps ops = { ctr_list_saves, ctr_list_remote_only, ctr_write_save, 0 };
    return ops;
}

std::vector<SaveLocation> resolve_save_locations(const WsVault* vault,
                                                 const TitleInfo& title,
                                                 bool* error) {
    std::vector<SaveLocation> results;
    if (error) *error = false;

    printf("[history] resolve_save_locations: title=%s\n", title.name.c_str());

    std::vector<uint8_t> raw_tree = extract_save_json(title);
    if (raw_tree.empty()) {
        printf("[history] resolve_save_locations: no local save data for %s\n",
               title.name.c_str());
        return results;
    }

    WsBuf savelist = normalize_title(title, raw_tree);
    if (!savelist.ptr) {
        printf("[history] resolve_save_locations: normalize failed (%s)\n", ffi_err());
        if (error) *error = true;
        return results;
    }

    std::vector<SaveListEntry> saves;
    if (!save_list_decode(savelist.ptr, savelist.len, &saves)) {
        printf("[history] resolve_save_locations: save_list_decode failed (%zu bytes)\n",
               (size_t)savelist.len);
        ws_buf_free(savelist);
        if (error) *error = true;
        return results;
    }
    if (saves.empty()) {
        ws_buf_free(savelist);
        if (title.source == SourceRom) {
            SaveLocation loc;
            loc.raw_tree = raw_tree;
            loc.group_key = rom_group_key(title.system, title.rom_id, "battery");
            loc.base_path = sync_base_path(vault, loc.group_key);
            printf("[history] resolve_save_locations: ROM %s has no local save -> %s\n",
                   title.rom_file_name.c_str(), loc.group_key.c_str());
            results.push_back(loc);
            return results;
        }
        printf("[history] resolve_save_locations: no saves after normalize for %s\n",
               title.name.c_str());
        return results;
    }

    std::string mtime = current_utc_time();
    for (size_t si = 0; si < saves.size(); si++) {
        std::string save_meta = json_set_mtime(saves[si].meta_json, mtime.c_str());

        WsBuf zip = {nullptr, 0};
        char* entry_json = ws_package(save_meta.c_str(), saves[si].files_ptr,
                                      saves[si].files_len, &zip);
        if (!entry_json) {
            printf("[history] resolve_save_locations: ws_package failed save %zu: %s\n",
                   si, ws_last_error() ? ws_last_error() : "unknown");
            // zip not allocated when ws_package fails — do not free (mirrors scan_save_decision)
            continue;
        }

        SaveLocation loc;
        loc.raw_tree  = raw_tree;
        loc.group_key = json_get_string(entry_json, "group_key");
        ws_string_free(entry_json);
        ws_buf_free(zip);

        if (loc.group_key.empty()) {
            printf("[history] resolve_save_locations: empty group_key for save %zu\n", si);
            continue;
        }

        loc.base_path = sync_base_path(vault, loc.group_key);
        printf("[history] resolve_save_locations: save %zu group_key=%s base_path=%s\n",
               si, loc.group_key.c_str(),
               loc.base_path.empty() ? "(empty)" : loc.base_path.c_str());

        results.push_back(loc);
    }

    ws_buf_free(savelist);
    printf("[history] resolve_save_locations: resolved %zu location(s) for %s\n",
           results.size(), title.name.c_str());
    return results;
}

