#include "sync.h"
#include "rom_saves.h"
#include "rom_parse.h"
#include "file_tree.h"

#include <chrono>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

struct Vault;
extern "C" {
#include "waystone.h"
}

static const char* ffi_err() { return ws_last_error() ? ws_last_error() : "unknown"; }

static unsigned long long now_ms() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static unsigned long long ms_since(unsigned long long t0) { return now_ms() - t0; }

static int nx_list_saves(void* ctx, const void* tp, LocalSaveSet& out) {
    const AccountUid* uid = static_cast<const AccountUid*>(ctx);
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    if (title.source == SourceRom) {
        std::vector<uint8_t> raw = extract_rom_saves(title);
        if (raw.empty()) {
            printf("[saves] %s: no local ROM save files\n", title.name.c_str());
            return 0;
        }
        printf("[sync] normalize rom system=%s rom_id=%s file=%s\n", title.system.c_str(),
               title.rom_id.c_str(), title.rom_file_name.c_str());
        WsBuf sl = ws_rom_keyed_normalize(title.system.c_str(), title.rom_id.c_str(),
                                          title.name.c_str(), title.rom_file_name.c_str(),
                                          raw.data(), raw.size());
        if (!sl.ptr) {
            printf("[sync] %s: ws_rom_keyed_normalize failed: %s\n", title.name.c_str(), ffi_err());
            return -1;
        }
        // adopt_normalized takes ownership of sl.ptr (freed there on decode failure too).
        if (!out.adopt_normalized(sl.ptr, sl.len, raw, rom_saves_mtime(title))) {
            printf("[sync] %s: save list decode failed\n", title.name.c_str());
            return -1;
        }
        printf("[sync] %s: normalized %zu ROM save(s)\n", title.name.c_str(), out.saves.size());
        return 0;
    }
    std::string mtime;
    std::vector<uint8_t> raw = extract_save_json(title, *uid, &mtime);
    if (raw.empty()) {
        printf("[saves] %s: no local save for this user\n", title.name.c_str());
        return 0;
    }
    WsBuf sl = ws_jksv_normalize("switch", raw.data(), raw.size());
    if (!sl.ptr) {
        printf("[sync] %s: ws_jksv_normalize failed: %s\n", title.name.c_str(), ffi_err());
        return -1;
    }
    if (!out.adopt_normalized(sl.ptr, sl.len, raw, mtime)) {
        printf("[sync] %s: save list decode failed\n", title.name.c_str());
        return -1;
    }
    printf("[sync] %s: normalized %zu save(s)\n", title.name.c_str(), out.saves.size());
    return 0;
}

// ROM saves that exist on the server but not on this SD yet (port of ctr_list_remote_only;
// gated on SourceRom && has_remote).
static int nx_list_remote_only(void*, const WsVault* vault, WebDavSession* dav, const void* tp,
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
    if (probe.empty()) {
        printf("[sync] remote-only %s: sync_base_path failed\n", game_key.c_str());
        return -1;
    }
    std::string game_dir = probe.substr(0, probe.rfind('/'));
    std::vector<std::string> hrefs;
    unsigned long long t0 = now_ms();
    int rc = webdav_propfind_s(dav, game_dir.c_str(), &hrefs);
    if (rc != 0) {
        printf("[sync] remote-only %s: PROPFIND failed rc=%d\n", game_key.c_str(), rc);
        return -1;
    }
    std::set<std::string> remote_slots;
    for (size_t i = 0; i < hrefs.size(); i++) remote_slots.insert(href_last_segment(hrefs[i]));
    printf("[sync] remote-only %s: %zu remote entries in %llu ms\n", game_key.c_str(),
           hrefs.size(), ms_since(t0));

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

static int nx_write_save(void* ctx, const void* tp, const std::string& group_key,
                         const uint8_t* tree, size_t tree_len) {
    const AccountUid* uid = static_cast<const AccountUid*>(ctx);
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    if (title.source == SourceRom) {
        std::string slot;
        size_t last_slash = group_key.rfind('/');
        if (last_slash != std::string::npos) slot = group_key.substr(last_slash + 1);
        WsBuf native = {nullptr, 0};
        int nrc = ws_rom_keyed_to_native(title.system.c_str(), slot.c_str(),
                                         title.rom_file_name.c_str(), tree, tree_len, &native);
        if (nrc != 0) {
            printf("[sync] ws_rom_keyed_to_native failed for %s: %s\n", group_key.c_str(), ffi_err());
            return -1;
        }
        std::vector<FileTreeEntry> entries;
        bool ok = file_tree_decode(native.ptr, native.len, &entries);
        ws_buf_free(native);
        if (!ok) {
            printf("[saves] rom restore %s: native tree decode failed\n", group_key.c_str());
            return -1;
        }
        printf("[saves] rom restore %s -> dir=%s (%zu file(s))\n", group_key.c_str(),
               title.save_dir.c_str(), entries.size());
        int rrc = write_rom_saves(title, entries);
        printf("[saves] rom restore %s rc=%d\n", group_key.c_str(), rrc);
        return rrc;
    }
    printf("[saves] write %s -> tid=%016lX (%zu bytes)\n", group_key.c_str(),
           (unsigned long)title.title_id, tree_len);
    int rc = write_save_files(title.title_id, *uid, tree, tree_len);
    printf("[saves] write %s rc=%d\n", group_key.c_str(), rc);
    return rc;
}

ShellOps nx_shell_ops(AccountUid* uid) {
    ShellOps ops;
    ops.list_saves = nx_list_saves;
    ops.list_remote_only = nx_list_remote_only;
    ops.write_save = nx_write_save;
    ops.ctx = uid;
    return ops;
}
