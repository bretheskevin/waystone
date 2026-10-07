#include "sync.h"
#include "json.h"
#include "file_tree.h"
#include "snapshot.h"
#include "snapshot_browse.h" // history_timestamp (shared)
#include "sync_summary.h"
#include "rom_parse.h"

#include <3ds.h>
#include <cstdio>
#include <cstring>
#include <set>

extern "C" {
#include "waystone.h"
}

static std::string make_base_path(const WsVault* vault,
                                  const std::string& group_key) {
    size_t p1 = group_key.find('/');
    size_t p2 = (p1 != std::string::npos) ? group_key.find('/', p1 + 1)
                                           : std::string::npos;
    if (p1 == std::string::npos || p2 == std::string::npos) {
        printf("FAIL: malformed group_key\n");
        return "";
    }
    std::string sys_str  = group_key.substr(0, p1);
    std::string game_str = group_key.substr(p1 + 1, p2 - p1 - 1);
    std::string slot_str = group_key.substr(p2 + 1);

    char* sys_seg  = ws_vault_path_segment(vault, sys_str.c_str());
    char* game_seg = ws_vault_path_segment(vault, game_str.c_str());
    char* slot_seg = ws_vault_path_segment(vault, slot_str.c_str());
    if (!sys_seg || !game_seg || !slot_seg) {
        printf("FAIL: ws_vault_path_segment returned null\n");
        ws_string_free(sys_seg);
        ws_string_free(game_seg);
        ws_string_free(slot_seg);
        return "";
    }

    std::string result = std::string(sys_seg) + "/" + game_seg + "/" + slot_seg;
    ws_string_free(sys_seg);
    ws_string_free(game_seg);
    ws_string_free(slot_seg);
    return result;
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

static void report_step(SyncProgress* prog, const char* label) {
    printf("[sync] step: %s\n", label);
    if (prog && prog->step) prog->step(prog->ctx, label);
}

static unsigned long long elapsed_ms(u64 since) {
    return (unsigned long long)(osGetTime() - since);
}

// Folders almost always exist, so PUT first and only create the parent on a missing-parent status.
static int put_creating_parent(WebDavSession* dav, const std::string& parent_dir,
                               const std::string& remote_path,
                               const uint8_t* data, size_t len,
                               const char* step_label, bool report_bytes,
                               SyncProgress* prog) {
    bool (*progress)(size_t, size_t, void*) = (report_bytes && prog) ? prog->bytes : nullptr;
    void* ctx = prog ? prog->ctx : nullptr;

    u64 t0 = osGetTime();
    int rc = webdav_put_s(dav, remote_path.c_str(), data, len, progress, ctx);
    printf("[sync] PUT %s (%zu bytes) rc=%d in %llu ms\n",
           remote_path.c_str(), len, rc, elapsed_ms(t0));
    if (!put_needs_parent_dir(rc)) return rc;

    report_step(prog, "Preparing server");
    printf("[sync] parent missing (status=%d) -- creating %s\n", rc, parent_dir.c_str());
    t0 = osGetTime();
    int mrc = webdav_mkdir_p_s(dav, parent_dir.c_str());
    if (mrc != 0) {
        printf("[sync] mkdir_p %s failed (err=%d) in %llu ms\n",
               parent_dir.c_str(), mrc, elapsed_ms(t0));
        return mrc;
    }
    printf("[sync] mkdir_p %s done in %llu ms\n", parent_dir.c_str(), elapsed_ms(t0));

    report_step(prog, step_label);
    if (progress) progress(0, len, ctx);
    t0 = osGetTime();
    rc = webdav_put_s(dav, remote_path.c_str(), data, len, progress, ctx);
    printf("[sync] PUT retry %s (%zu bytes) rc=%d in %llu ms\n",
           remote_path.c_str(), len, rc, elapsed_ms(t0));
    return rc;
}

// Fetch + decrypt remote heads under d.base_path and fill d.decision_type/pull_hash/winner.
// d.local_hash may be empty (remote-only ROM slot -> core decides Pull).
static void decide_from_heads(const WsVault* vault, SaveDecision& d, const char* mtime,
                              const char* device_id, int policy, WebDavSession* dav) {
    std::string heads_path = d.base_path + "/heads";
    std::vector<std::string> hrefs;
    if (webdav_propfind_s(dav, heads_path.c_str(), &hrefs) != 0) {
        printf("FAIL: PROPFIND %s\n", heads_path.c_str());
        return;
    }
    if (hrefs.empty()) {
        printf("no remote heads found -- skipping\n");
        d.decision_type = "in_sync";
        return;
    }

    d.heads_array = "[";
    bool first_head = true;
    for (size_t hi = 0; hi < hrefs.size(); hi++) {
        const std::string& href = hrefs[hi];
        if (href.size() < 5 || href.compare(href.size() - 5, 5, ".json") != 0) continue;
        std::vector<uint8_t> enc_data;
        if (webdav_get_s(dav, href.c_str(), &enc_data) != 0) continue;
        WsBuf decrypted = ws_vault_decrypt_heads(vault, enc_data.data(), enc_data.size());
        if (!decrypted.ptr) {
            printf("\n  WARN: ws_vault_decrypt_heads failed for %s: %s\n", href.c_str(),
                   ws_last_error() ? ws_last_error() : "unknown");
            continue;
        }
        std::string head_obj(reinterpret_cast<const char*>(decrypted.ptr), decrypted.len);
        ws_buf_free(decrypted);
        if (!first_head) d.heads_array += ",";
        d.heads_array += head_obj;
        first_head = false;
    }
    d.heads_array += "]";
    if (first_head) {
        printf("no decryptable heads -- skipping\n");
        d.decision_type = "in_sync";
        return;
    }

    const char* local_hash_ptr = d.local_hash.empty() ? nullptr : d.local_hash.c_str();
    char* decision_json = ws_decide_pull(local_hash_ptr, mtime, d.heads_array.c_str(), device_id, policy);
    if (!decision_json) {
        printf("FAIL ws_decide_pull: %s\n", ws_last_error() ? ws_last_error() : "unknown");
        return;
    }
    d.decision_type = json_get_string(decision_json, "type");
    if (d.decision_type == "pull") {
        d.pull_hash = json_get_string(decision_json, "head_hash");
    } else if (d.decision_type == "conflict_resolved") {
        d.winner = json_get_string(decision_json, "winner");
        if (d.winner == "remote") {
            char* folded = ws_fold_heads(d.heads_array.c_str());
            if (folded) {
                d.pull_hash = json_get_string(folded, "hash");
                ws_string_free(folded);
            }
        }
    }
    ws_string_free(decision_json);
}

SaveDecision scan_save_decision(const WsVault* vault, const char* save_meta,
                                const char* mtime, const char* device_id,
                                int policy, WebDavSession* dav,
                                const std::vector<uint8_t>& raw_tree,
                                const uint8_t* files_ptr, size_t files_len) {
    SaveDecision d;
    d.raw_tree = raw_tree;

    WsBuf zip = {nullptr, 0};
    char* entry_json = ws_package(save_meta, files_ptr, files_len, &zip);
    if (!entry_json) {
        printf("FAIL ws_package: %s\n", ws_last_error() ? ws_last_error() : "unknown");
        return d;
    }
    d.local_hash = json_get_nested_string(entry_json, "content", "hash");
    d.group_key  = json_get_string(entry_json, "group_key");
    ws_string_free(entry_json);
    ws_buf_free(zip);
    if (d.group_key.empty()) {
        printf("FAIL: could not parse SaveEntry group_key\n");
        return d;
    }
    d.base_path = make_base_path(vault, d.group_key);
    if (d.base_path.empty()) return d;

    decide_from_heads(vault, d, mtime, device_id, policy, dav);
    return d;
}

int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::vector<uint8_t>& raw_tree, const TitleInfo& title,
                        WebDavSession* dav, SyncProgress* prog) {
    char* blob_name = ws_vault_blob_name(vault, pull_hash.c_str());
    if (!blob_name) {
        printf("  FAIL: ws_vault_blob_name: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        return -1;
    }
    std::string blob_remote = base_path + "/blobs/" + blob_name + ".bin";
    ws_string_free(blob_name);

    report_step(prog, "Downloading");
    printf("[sync] download blob start\n");
    std::vector<uint8_t> enc_blob;
    int grc = webdav_get_s(dav, blob_remote.c_str(), &enc_blob,
                         prog ? prog->bytes : nullptr, prog ? prog->ctx : nullptr);
    if (grc == 1) { printf("  FAIL: blob not found (404)\n"); return -1; }
    else if (grc != 0) { printf("  FAIL: GET blob error (rc=%d)\n", grc); return -1; }
    printf("[sync] download blob done: %zu bytes\n", enc_blob.size());

    WsBuf decrypted_blob = ws_vault_decrypt_blob(vault, enc_blob.data(), enc_blob.size());
    if (!decrypted_blob.ptr) {
        printf("  FAIL: ws_vault_decrypt_blob: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        return -1;
    }

    size_t blob_len = decrypted_blob.len;
    WsBuf file_tree = ws_unzip(decrypted_blob.ptr, decrypted_blob.len);
    ws_buf_free(decrypted_blob);
    if (!file_tree.ptr) {
        printf("  FAIL: ws_unzip: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        return -1;
    }
    printf("[sync] unzip ok (blob %zu bytes -> file_tree %zu bytes)\n",
           blob_len, (size_t)file_tree.len);

    report_step(prog, "Restoring");
    {
        std::string sanitized = snapshot_sanitize_key(group_key);
        std::string snap_ts = history_timestamp();
        std::string backup_dir = std::string("sdmc:/waystone/backups/") +
                                 sanitized + "/" + snap_ts;
        if (!write_snapshot(backup_dir.c_str(), raw_tree.data(), raw_tree.size())) {
            printf("  WARN: safety snapshot failed for %s, skipping restore\n",
                   group_key.c_str());
            ws_buf_free(file_tree);
            return -1;
        }
        std::string backup_root = std::string("sdmc:/waystone/backups/") + sanitized;
        if (!snapshot_prune(backup_root.c_str(), SNAPSHOT_KEEP)) {
            printf("  [snapshot] WARN: prune failed for %s (non-fatal)\n",
                   group_key.c_str());
        }
    }

    // Parse slot from group_key ("3ds/<game>/<slot>"): "extdata" routes to
    // SaveExtdata; the "main" slot routes by title origin (TWL / SD) via
    // main_save_kind.
    SaveArchiveKind kind = main_save_kind(title);
    {
        size_t last_slash = group_key.rfind('/');
        if (last_slash != std::string::npos) {
            std::string slot = group_key.substr(last_slash + 1);
            if (slot == "extdata") kind = SaveExtdata;
        }
    }
    printf("[saves] restore routing group_key=%s -> kind=%s\n",
           group_key.c_str(), save_kind_name(kind));

    if (title.source == SourceRom) {
        std::string slot = group_key.substr(group_key.rfind('/') + 1);
        WsBuf native = {nullptr, 0};
        int nrc = ws_rom_keyed_to_native(title.system.c_str(), slot.c_str(),
                                         title.rom_file_name.c_str(),
                                         file_tree.ptr, file_tree.len, &native);
        ws_buf_free(file_tree);
        if (nrc != 0) {
            printf("[sync] ws_rom_keyed_to_native failed for %s: %s\n", group_key.c_str(), ffi_err());
            return -1;
        }
        printf("[saves] rom restore %s -> dir=%s (%zu bytes native tree)\n",
               group_key.c_str(), title.save_dir.c_str(), (size_t)native.len);
        int rrc = write_save_files(title, native.ptr, native.len, SaveRomFile);
        ws_buf_free(native);
        return rrc;
    }

    int wrc = write_save_files(title, file_tree.ptr, file_tree.len, kind);
    ws_buf_free(file_tree);
    return wrc;
}

// ROM saves that exist on the server but not on this SD yet: restore-only decisions, one per remote slot.
static void append_remote_only_rom_decisions(const WsVault* vault, const TitleInfo& title,
                                             const char* device_id, int policy,
                                             WebDavSession* dav,
                                             const std::vector<uint8_t>& raw_tree,
                                             std::vector<SaveDecision>& results) {
    if (title.source != SourceRom || !title.has_remote) return;
    const std::string game_key = rom_group_key(title.system, title.rom_id, 0);
    char* slots_c = ws_rom_keyed_slots(title.system.c_str());
    if (!slots_c) { printf("[sync] remote-only %s: ws_rom_keyed_slots failed: %s\n", game_key.c_str(), ffi_err()); return; }
    std::string slots(slots_c);
    ws_string_free(slots_c);

    std::string probe = make_base_path(vault, rom_group_key(title.system, title.rom_id, "battery"));
    if (probe.empty()) return;
    std::string game_dir = probe.substr(0, probe.rfind('/'));
    std::vector<std::string> hrefs;
    u64 t0 = osGetTime();
    int rc = webdav_propfind_s(dav, game_dir.c_str(), &hrefs);
    if (rc != 0) { printf("[sync] remote-only %s: PROPFIND failed rc=%d\n", game_key.c_str(), rc); return; }
    std::set<std::string> remote_slots;
    for (size_t i = 0; i < hrefs.size(); i++) remote_slots.insert(href_last_segment(hrefs[i]));
    printf("[sync] remote-only %s: %zu remote entr%s in %llu ms\n", game_key.c_str(),
           hrefs.size(), hrefs.size() == 1 ? "y" : "ies", elapsed_ms(t0));

    std::string mtime = current_utc_time();
    size_t start = 0;
    while (start < slots.size()) {
        size_t nl = slots.find('\n', start);
        if (nl == std::string::npos) nl = slots.size();
        std::string slot = slots.substr(start, nl - start);
        start = nl + 1;
        if (slot.empty()) continue;
        std::string gk = rom_group_key(title.system, title.rom_id, slot.c_str());
        bool local = false;
        for (size_t i = 0; i < results.size(); i++) if (results[i].group_key == gk) { local = true; break; }
        if (local) continue;
        std::string base = make_base_path(vault, gk);
        if (base.empty() || !remote_slots.count(base.substr(base.rfind('/') + 1))) continue;
        SaveDecision d;
        d.raw_tree = raw_tree;
        d.group_key = gk;
        d.base_path = base;
        decide_from_heads(vault, d, mtime.c_str(), device_id, policy, dav);
        printf("[sync] remote-only %s -> %s\n", gk.c_str(),
               d.decision_type.empty() ? "(no decision)" : d.decision_type.c_str());
        results.push_back(d);
    }
}

std::vector<SaveDecision> scan_title(const WsVault* vault, const TitleInfo& title,
                                     const char* device_id, int policy,
                                     WebDavSession* dav, bool* error,
                                     SyncProgress* prog) {
    std::vector<SaveDecision> results;
    if (error) *error = false;

    std::vector<uint8_t> raw_tree = extract_save_json(title);
    if (raw_tree.empty()) return results;

    size_t raw_len = raw_tree.size();
    WsBuf savelist = normalize_title(title, raw_tree);
    if (!savelist.ptr) {
        printf("  normalize failed (%s)\n", ffi_err());
        if (error) *error = true;
        return results;
    }

    std::vector<SaveListEntry> saves;
    if (!save_list_decode(savelist.ptr, savelist.len, &saves)) {
        printf("[sync] save_list_decode failed (%zu bytes)\n", (size_t)savelist.len);
        ws_buf_free(savelist);
        if (error) *error = true;
        return results;
    }
    printf("[sync] normalize ok (raw %zu -> %zu bytes, %zu saves)\n",
           raw_len, (size_t)savelist.len, saves.size());
    // ROM titles without a local save still probe the server for remote-only slots below.
    if (saves.empty() && title.source != SourceRom) { ws_buf_free(savelist); return results; }
    report_step(prog, "Checking server");

    std::string mtime = current_utc_time();
    for (size_t si = 0; si < saves.size(); si++) {
        std::string save_meta = json_set_mtime(saves[si].meta_json, mtime.c_str());
        SaveDecision d = scan_save_decision(vault, save_meta.c_str(), mtime.c_str(),
                                            device_id, policy, dav, raw_tree,
                                            saves[si].files_ptr, saves[si].files_len);
        results.push_back(d);
    }
    ws_buf_free(savelist);
    append_remote_only_rom_decisions(vault, title, device_id, policy, dav, raw_tree, results);
    return results;
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
            loc.base_path = make_base_path(vault, loc.group_key);
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

        loc.base_path = make_base_path(vault, loc.group_key);
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

int push_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               WebDavSession* dav,
               PushStats* stats,
               SyncProgress* prog) {
    PushStats local_stats = {0, 0};
    PushStats* st = stats ? stats : &local_stats;
    *st = local_stats;

    report_step(prog, "Reading save");
    printf("  Extracting save data...\n");
    std::vector<uint8_t> raw_tree = extract_save_json(title);
    if (raw_tree.empty()) {
        printf("  No save data found.\n");
        return 0;
    }

    report_step(prog, "Normalizing");
    printf("  Normalizing...\n");
    size_t raw_len = raw_tree.size();
    WsBuf savelist = normalize_title(title, raw_tree);
    std::vector<uint8_t>().swap(raw_tree); // eager-free
    if (!savelist.ptr) {
        printf("  normalize failed (%s)\n", ffi_err());
        return -1;
    }

    std::vector<SaveListEntry> saves;
    if (!save_list_decode(savelist.ptr, savelist.len, &saves)) {
        printf("[sync] save_list_decode failed (%zu bytes)\n", (size_t)savelist.len);
        ws_buf_free(savelist);
        return -1;
    }
    printf("[sync] normalize ok (raw %zu -> %zu bytes, %zu saves)\n",
           raw_len, (size_t)savelist.len, saves.size());

    if (saves.empty()) {
        printf("  No normalized saves produced.\n");
        ws_buf_free(savelist);
        return 0;
    }

    std::string mtime = current_utc_time();
    int pushed = 0;

    for (size_t si = 0; si < saves.size(); si++) {
        report_step(prog, "Encrypting");
        printf("  Save %zu/%zu: ", si + 1, saves.size());

        std::string save_meta = json_set_mtime(saves[si].meta_json, mtime.c_str());

        WsBuf zip = {nullptr, 0};
        u64 t0 = osGetTime();
        char* entry_json = ws_package(save_meta.c_str(), saves[si].files_ptr,
                                      saves[si].files_len, &zip);
        unsigned long long package_ms = elapsed_ms(t0);
        if (!entry_json) {
            printf("FAIL ws_package: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            st->failed++;
            continue;
        }

        std::string content_hash = json_get_nested_string(entry_json, "content", "hash");
        std::string group_key = json_get_string(entry_json, "group_key");
        ws_string_free(entry_json);

        if (content_hash.empty() || group_key.empty()) {
            printf("FAIL: could not parse SaveEntry\n");
            ws_buf_free(zip);
            st->failed++;
            continue;
        }

        printf("group=%s hash=%s\n", group_key.c_str(),
               content_hash.substr(0, 12).c_str());
        printf("[sync] package done: zip %zu bytes in %llu ms\n", (size_t)zip.len, package_ms);

        std::string base_path = make_base_path(vault, group_key);
        if (base_path.empty()) {
            ws_buf_free(zip);
            st->failed++;
            continue;
        }

        std::string blobs_path   = base_path + "/blobs";
        std::string heads_path   = base_path + "/heads";
        std::string history_path = base_path + "/history";

        size_t zip_len = zip.len;
        t0 = osGetTime();
        WsBuf encrypted = ws_vault_encrypt_blob(vault, zip.ptr, zip.len);
        ws_buf_free(zip);
        if (!encrypted.ptr) {
            printf("FAIL: ws_vault_encrypt_blob: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            st->failed++;
            continue;
        }
        printf("[sync] encrypt done: %zu -> %zu bytes in %llu ms\n",
               zip_len, (size_t)encrypted.len, elapsed_ms(t0));

        char* blob_name = ws_vault_blob_name(vault, content_hash.c_str());
        if (!blob_name) {
            printf("FAIL: ws_vault_blob_name\n");
            ws_buf_free(encrypted);
            st->failed++;
            continue;
        }
        std::string blob_remote = blobs_path + "/" + blob_name + ".bin";
        ws_string_free(blob_name);

        report_step(prog, "Checking server");
        t0 = osGetTime();
        int exists = webdav_exists_s(dav, blob_remote.c_str());
        printf("[sync] blob exists check rc=%d in %llu ms\n", exists, elapsed_ms(t0));
        if (exists <= 0) {
            report_step(prog, "Uploading");
            if (prog && prog->bytes) prog->bytes(0, encrypted.len, prog->ctx);
            printf("[sync] upload blob start: %zu bytes\n", (size_t)encrypted.len);
            int prc = put_creating_parent(dav, blobs_path, blob_remote,
                                          encrypted.ptr, encrypted.len,
                                          "Uploading", true, prog);
            if (prc != 0) {
                printf("FAIL: PUT blob (rc=%d)\n", prc);
                ws_buf_free(encrypted);
                st->failed++;
                continue;
            }
            printf("[sync] upload blob done: %zu bytes\n", (size_t)encrypted.len);
            st->uploaded++;
        } else {
            report_step(prog, "Already up to date");
            printf("[sync] blob already on server -- upload skipped\n");
        }
        ws_buf_free(encrypted);

        report_step(prog, "Updating index");
        std::string head_json = build_device_head_json(
            device_id, content_hash.c_str(), mtime.c_str());
        WsBuf encrypted_head = ws_vault_encrypt_heads(
            vault,
            reinterpret_cast<const uint8_t*>(head_json.data()),
            head_json.size());
        if (!encrypted_head.ptr) {
            printf("FAIL: ws_vault_encrypt_heads: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            st->failed++;
            continue;
        }

        std::string head_remote = heads_path + "/" + device_id + ".json";
        int hrc = put_creating_parent(dav, heads_path, head_remote,
                                      encrypted_head.ptr, encrypted_head.len,
                                      "Updating index", false, prog);
        if (hrc != 0) {
            printf("FAIL: PUT head (rc=%d)\n", hrc);
            ws_buf_free(encrypted_head);
            st->failed++;
            continue;
        }

        std::string ts = history_timestamp();
        std::string hist_remote = history_path + "/" + ts + "-" + device_id + ".json";
        WsBuf encrypted_hist = ws_vault_encrypt_heads(
            vault,
            reinterpret_cast<const uint8_t*>(head_json.data()),
            head_json.size());
        if (encrypted_hist.ptr) {
            if (put_creating_parent(dav, history_path, hist_remote,
                                    encrypted_hist.ptr, encrypted_hist.len,
                                    "Updating index", false, prog) != 0) {
                printf("  WARN: PUT history failed (non-fatal)\n");
            }
            ws_buf_free(encrypted_hist);
        }
        ws_buf_free(encrypted_head);

        pushed++;
        printf("  Pushed OK.\n");
    }

    ws_buf_free(savelist);
    printf("[sync] push_title %s done (pushed=%d uploaded=%d failed=%d)\n",
           title.name.c_str(), pushed, st->uploaded, st->failed);
    return pushed;
}

int pull_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               WebDavSession* dav,
               PullStats* stats,
               SyncProgress* prog) {
    PullStats local_stats = {0, 0, 0, 0};
    PullStats* st = stats ? stats : &local_stats;
    *st = local_stats;

    report_step(prog, "Reading save");
    printf("  Extracting local save data for pull comparison...\n");
    bool had_error = false;
    std::vector<SaveDecision> decisions = scan_title(vault, title, device_id,
                                                     0 /* NewestWins */, dav, &had_error,
                                                     prog);
    if (had_error) {
        printf("[sync] pull_title %s: scan_title failed\n", title.name.c_str());
        return -1;
    }
    if (decisions.empty()) {
        printf("  No local saves or nothing to scan.\n");
        return 0;
    }

    int pulled = 0;
    for (size_t i = 0; i < decisions.size(); i++) {
        const SaveDecision& d = decisions[i];
        printf("  Pull %zu/%zu: ", i + 1, decisions.size());
        if (d.decision_type == "in_sync") {
            printf("in_sync\n"); continue;
        } else if (d.decision_type == "push") {
            printf("decision=push, no pull needed\n"); continue;
        } else if (d.decision_type == "conflict_needs_input") {
            printf("conflict_needs_input -- manual resolution required (skipping)\n");
            st->conflicts++;
            continue;
        } else if (d.decision_type == "conflict_resolved" && d.winner == "local") {
            printf("conflict_resolved winner=local, no pull needed\n"); continue;
        } else if (d.decision_type.empty()) {
            printf("no decision (server check failed) -- skipping\n");
            st->scan_failures++;
            continue;
        } else if (d.decision_type != "pull" && d.decision_type != "conflict_resolved") {
            printf("unknown decision type: %s\n", d.decision_type.c_str()); continue;
        }
        if (d.pull_hash.empty()) {
            printf("FAIL: could not determine pull hash\n");
            st->restore_failures++;
            continue;
        }
        printf("pulling hash=%.12s...\n", d.pull_hash.c_str());
        int rc = restore_remote_save(vault, d.pull_hash, d.base_path, d.group_key,
                                     d.raw_tree, title, dav, prog);
        if (rc == 0) {
            printf("  Pulled OK.\n"); pulled++;
        } else {
            printf("  WARN: restore failure (%d)\n", rc);
            st->restore_failures++;
        }
    }
    st->pulled = pulled;
    printf("[sync] pull_title %s done (pulled=%d conflicts=%d restore_failures=%d "
           "scan_failures=%d)\n", title.name.c_str(), pulled, st->conflicts,
           st->restore_failures, st->scan_failures);
    return pulled;
}
