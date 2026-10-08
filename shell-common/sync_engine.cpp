#include "sync_engine.h"
#include "sync_rules.h"
#include "json.h"
#include "file_tree.h"  // save_list_decode
#include "snapshot.h"
#include "snapshot_browse.h"  // history_timestamp

#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/time.h>

extern "C" {
#include "waystone.h"
}

static const char* kBackupsRoot = "sdmc:/waystone/backups/";

static const char* ffi_err() {
    const char* e = ws_last_error();
    return e ? e : "unknown";
}

static unsigned long long now_ms() {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)(tv.tv_usec / 1000);
}

static unsigned long long since_ms(unsigned long long t0) { return now_ms() - t0; }

static std::string utc_now() { return utc_iso_from_epoch((long long)time(0)); }

static void report_step(const SyncProgress* prog, const char* label) {
    printf("[sync] step: %s\n", label);
    if (prog && prog->step) prog->step(prog->ctx, label);
}

static void tally_fail(TitleTally& t, const char* why) {
    t.failed = true;
    if (t.reason.empty()) t.reason = why;
}

SyncEngineCfg sync_cfg_from(const WaystoneShellConfig& cfg, const char* device_id) {
    SyncEngineCfg c;
    c.conflict_policy = (int)cfg.conflict_policy;
    c.safety_backup = cfg.safety_backup;
    c.device_id = device_id;
    return c;
}

LocalSaveSet::LocalSaveSet() : savelist_ptr_(0), savelist_len_(0) {}

LocalSaveSet::~LocalSaveSet() {
    if (savelist_ptr_) {
        WsBuf b;
        b.ptr = savelist_ptr_;
        b.len = savelist_len_;
        ws_buf_free(b);
    }
}

bool LocalSaveSet::adopt_normalized(uint8_t* ptr, size_t len, std::vector<uint8_t>& raw,
                                    const std::string& local_mtime) {
    std::vector<SaveListEntry> entries;
    if (savelist_ptr_ || !save_list_decode(ptr, len, &entries)) {
        printf("[sync] adopt_normalized: %s (%zu bytes) -- buffer freed\n",
               savelist_ptr_ ? "BUG: called twice" : "save_list_decode failed", len);
        WsBuf b;
        b.ptr = ptr;
        b.len = len;
        ws_buf_free(b);
        return false;
    }
    savelist_ptr_ = ptr;
    savelist_len_ = len;
    raw_tree.swap(raw);
    for (size_t i = 0; i < entries.size(); i++) {
        LocalSave s;
        s.meta_json = entries[i].meta_json;
        s.files_ptr = entries[i].files_ptr;
        s.files_len = entries[i].files_len;
        s.local_mtime = local_mtime;
        saves.push_back(s);
    }
    printf("[sync] adopt_normalized: %zu save(s), raw tree %zu bytes, local mtime '%s'\n",
           saves.size(), raw_tree.size(), local_mtime.empty() ? "(unknown)" : local_mtime.c_str());
    return true;
}

// json_set_mtime + ws_package for one local save. The zip is handed to zip_out (caller frees)
// or freed here when zip_out is null. false = package/parse failed; nothing left to free.
static bool package_local(const LocalSave& s, std::string& mtime, std::string& hash,
                          std::string& group_key, WsBuf* zip_out) {
    mtime = s.local_mtime.empty() ? utc_now() : s.local_mtime;
    std::string meta = json_set_mtime(s.meta_json, mtime.c_str());
    WsBuf zip = {0, 0};
    char* entry = ws_package(meta.c_str(), s.files_ptr, s.files_len, &zip);
    if (!entry) {
        printf("[sync] ws_package failed: %s\n", ffi_err());
        return false;  // zip is not allocated when ws_package fails
    }
    hash = json_get_nested_string(entry, "content", "hash");
    group_key = json_get_string(entry, "group_key");
    ws_string_free(entry);
    if (hash.empty() || group_key.empty()) {
        printf("[sync] could not parse SaveEntry (group_key='%s')\n", group_key.c_str());
        ws_buf_free(zip);
        return false;
    }
    if (zip_out) *zip_out = zip;
    else ws_buf_free(zip);
    return true;
}

std::string sync_base_path(const WsVault* vault, const std::string& group_key) {
    size_t p1 = group_key.find('/');
    size_t p2 = (p1 != std::string::npos) ? group_key.find('/', p1 + 1) : std::string::npos;
    if (p1 == std::string::npos || p2 == std::string::npos) {
        printf("[sync] base_path: malformed group_key '%s'\n", group_key.c_str());
        return "";
    }
    std::string sys_str = group_key.substr(0, p1);
    std::string game_str = group_key.substr(p1 + 1, p2 - p1 - 1);
    std::string slot_str = group_key.substr(p2 + 1);
    char* sys_seg = ws_vault_path_segment(vault, sys_str.c_str());
    char* game_seg = ws_vault_path_segment(vault, game_str.c_str());
    char* slot_seg = ws_vault_path_segment(vault, slot_str.c_str());
    if (!sys_seg || !game_seg || !slot_seg) {
        printf("[sync] base_path: ws_vault_path_segment failed for %s: %s\n",
               group_key.c_str(), ffi_err());
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

// PUT first; only on a missing-parent status (409/404) create the parent and retry once.
static int put_creating_parent(WebDavSession* dav, const std::string& parent_dir,
                               const std::string& remote_path, const uint8_t* data, size_t len,
                               const char* step_label, bool report_bytes,
                               const SyncProgress* prog) {
    bool (*progress)(size_t, size_t, void*) = (report_bytes && prog) ? prog->bytes : 0;
    void* ctx = prog ? prog->ctx : 0;

    unsigned long long t0 = now_ms();
    int rc = webdav_put_s(dav, remote_path.c_str(), data, len, progress, ctx);
    printf("[sync] PUT %s (%zu bytes) rc=%d in %llu ms\n", remote_path.c_str(), len, rc, since_ms(t0));
    if (!put_needs_parent_dir(rc)) return rc;

    report_step(prog, "Preparing server");
    printf("[sync] parent missing (status=%d) -- creating %s\n", rc, parent_dir.c_str());
    t0 = now_ms();
    int mrc = webdav_mkdir_p_s(dav, parent_dir.c_str());
    if (mrc != 0) {
        printf("[sync] mkdir_p %s failed (err=%d) in %llu ms\n", parent_dir.c_str(), mrc, since_ms(t0));
        return mrc;
    }
    printf("[sync] mkdir_p %s done in %llu ms\n", parent_dir.c_str(), since_ms(t0));

    report_step(prog, step_label);
    if (progress) progress(0, len, ctx);
    t0 = now_ms();
    rc = webdav_put_s(dav, remote_path.c_str(), data, len, progress, ctx);
    printf("[sync] PUT retry %s (%zu bytes) rc=%d in %llu ms\n", remote_path.c_str(), len, rc, since_ms(t0));
    return rc;
}

// heads/<device_id>.json = {device_id, hash, mtime}; with_history adds a history entry (push only).
static int put_device_head(const WsVault* vault, const std::string& base_path,
                           const char* device_id, const std::string& hash,
                           const std::string& mtime, bool with_history, WebDavSession* dav,
                           const SyncProgress* prog) {
    std::string heads_path = base_path + "/heads";
    std::string head_json = build_device_head_json(device_id, hash.c_str(), mtime.c_str());
    WsBuf enc = ws_vault_encrypt_heads(vault, reinterpret_cast<const uint8_t*>(head_json.data()),
                                       head_json.size());
    if (!enc.ptr) {
        printf("[sync] ws_vault_encrypt_heads failed: %s\n", ffi_err());
        return -1;
    }
    std::string head_remote = heads_path + "/" + device_id + ".json";
    int hrc = put_creating_parent(dav, heads_path, head_remote, enc.ptr, enc.len,
                                  "Updating index", false, prog);
    ws_buf_free(enc);
    if (hrc != 0) {
        printf("[sync] PUT head %s failed (rc=%d)\n", head_remote.c_str(), hrc);
        return -1;
    }
    printf("[sync] head %s -> hash=%.12s mtime=%s\n", head_remote.c_str(), hash.c_str(), mtime.c_str());
    if (!with_history) return 0;

    std::string history_path = base_path + "/history";
    std::string hist_remote = history_path + "/" + history_timestamp() + "-" + device_id + ".json";
    WsBuf eh = ws_vault_encrypt_heads(vault, reinterpret_cast<const uint8_t*>(head_json.data()),
                                      head_json.size());
    if (!eh.ptr) {
        printf("[sync] WARN: encrypt history entry failed: %s (non-fatal)\n", ffi_err());
        return 0;
    }
    if (put_creating_parent(dav, history_path, hist_remote, eh.ptr, eh.len, "Updating index",
                            false, prog) != 0)
        printf("[sync] WARN: PUT history %s failed (non-fatal)\n", hist_remote.c_str());
    ws_buf_free(eh);
    return 0;
}

// Fetch + decrypt heads under d.base_path and fill the decision. d.local_hash may be empty.
static void decide_from_heads(const WsVault* vault, SaveDecision& d, int policy,
                              const char* device_id, WebDavSession* dav) {
    std::string heads_path = d.base_path + "/heads";
    std::vector<std::string> hrefs;
    unsigned long long t0 = now_ms();
    int prc = webdav_propfind_s(dav, heads_path.c_str(), &hrefs);
    if (prc != 0) {
        printf("[sync] decide %s: PROPFIND heads failed (err=%d)\n", d.group_key.c_str(), prc);
        return;
    }

    size_t json_hrefs = 0, decrypted = 0;
    std::string arr = "[";
    for (size_t i = 0; i < hrefs.size(); i++) {
        if (!is_head_href(hrefs[i])) continue;
        json_hrefs++;
        std::vector<uint8_t> enc;
        int grc = webdav_get_s(dav, hrefs[i].c_str(), &enc);
        if (grc != 0) {
            printf("[sync] decide %s: GET head %s failed (rc=%d)\n", d.group_key.c_str(),
                   hrefs[i].c_str(), grc);
            continue;
        }
        WsBuf dec = ws_vault_decrypt_heads(vault, enc.data(), enc.size());
        if (!dec.ptr) {
            printf("[sync] WARN: decrypt head %s failed: %s\n", hrefs[i].c_str(), ffi_err());
            continue;
        }
        if (decrypted) arr += ",";
        arr.append(reinterpret_cast<const char*>(dec.ptr), dec.len);
        ws_buf_free(dec);
        decrypted++;
    }
    arr += "]";
    const bool have_local = !d.local_hash.empty();
    HeadsOutcome ho = classify_heads(json_hrefs, decrypted, have_local);
    printf("[sync] decide %s: %zu head file(s), %zu readable in %llu ms\n", d.group_key.c_str(),
           json_hrefs, decrypted, since_ms(t0));
    if (ho == HeadsOutcome::Push) {
        d.decision_type = "push";
        printf("[sync] decide %s -> push (no remote heads: first upload)\n", d.group_key.c_str());
        return;
    }
    if (ho == HeadsOutcome::InSync) {
        d.decision_type = "in_sync";
        printf("[sync] decide %s -> in_sync (nothing local, nothing remote)\n", d.group_key.c_str());
        return;
    }
    if (ho == HeadsOutcome::Failed) {
        printf("[sync] decide %s -> FAILED (heads present but none readable)\n", d.group_key.c_str());
        return;
    }

    d.heads_array = arr;
    d.own_head_hash = own_head_hash(arr, device_id);
    char* folded = ws_fold_heads(arr.c_str());
    if (folded) {
        d.head_hash = json_get_string(folded, "hash");
        d.head_device_id = json_get_string(folded, "device_id");
        d.head_mtime = json_get_string(folded, "mtime");
        ws_string_free(folded);
    } else {
        printf("[sync] WARN: ws_fold_heads failed for %s: %s\n", d.group_key.c_str(), ffi_err());
    }

    std::string mtime_arg = d.local_mtime.empty() ? utc_now() : d.local_mtime;
    char* dj = ws_decide_pull(have_local ? d.local_hash.c_str() : 0, mtime_arg.c_str(),
                              arr.c_str(), device_id, policy);
    if (!dj) {
        printf("[sync] decide %s: ws_decide_pull failed: %s\n", d.group_key.c_str(), ffi_err());
        return;
    }
    d.decision_type = json_get_string(dj, "type");
    if (d.decision_type == "pull") {
        d.pull_hash = json_get_string(dj, "head_hash");
    } else if (d.decision_type == "conflict_resolved") {
        d.winner = json_get_string(dj, "winner");
        if (d.winner == "remote") d.pull_hash = d.head_hash;
    }
    ws_string_free(dj);
    printf("[sync] decide %s -> %s%s%s (policy=%d local=%.12s mtime=%s own=%.12s head_mtime=%s)\n",
           d.group_key.c_str(), d.decision_type.c_str(), d.winner.empty() ? "" : " winner=",
           d.winner.c_str(), policy, d.local_hash.c_str(),
           d.local_mtime.empty() ? "(unknown)" : d.local_mtime.c_str(),
           d.own_head_hash.c_str(), d.head_mtime.c_str());
}

static SaveDecision decide_local_save(const WsVault* vault, const LocalSave& s, int index,
                                      const SyncEngineCfg& cfg, int cfg_policy,
                                      WebDavSession* dav) {
    SaveDecision d;
    d.save_index = index;
    d.local_mtime = s.local_mtime;
    std::string meta_mtime;
    if (!package_local(s, meta_mtime, d.local_hash, d.group_key, 0)) {
        printf("[sync] decide save %d: packaging failed (group_key='%s')\n", index,
               d.group_key.c_str());
        d.local_hash.clear();
        return d;
    }
    d.base_path = sync_base_path(vault, d.group_key);
    if (d.base_path.empty()) return d;
    int policy = effective_policy(cfg_policy, !d.local_mtime.empty());
    if (policy != cfg_policy)
        printf("[sync] %s: no usable local mtime -> NewestWins treated as Prompt\n",
               d.group_key.c_str());
    decide_from_heads(vault, d, policy, cfg.device_id, dav);
    return d;
}

// Phase 1 of a title: enumerate + decide every save. No server writes happen here.
static int decide_title(const WsVault* vault, const void* title, const char* name,
                        const ShellOps& ops, const SyncEngineCfg& cfg, int cfg_policy,
                        WebDavSession* dav, const SyncProgress* prog, LocalSaveSet& set,
                        std::vector<SaveDecision>& out) {
    report_step(prog, "Reading save");
    unsigned long long t0 = now_ms();
    int lrc = ops.list_saves(ops.ctx, title, set);
    if (lrc != 0) {
        printf("[sync] %s: list_saves failed (err=%d)\n", name, lrc);
        return -1;
    }
    printf("[sync] %s: %zu local save(s), raw tree %zu bytes in %llu ms\n", name,
           set.saves.size(), set.raw_tree.size(), since_ms(t0));

    if (!set.saves.empty()) report_step(prog, "Checking server");
    std::vector<std::string> local_gks;
    for (size_t i = 0; i < set.saves.size(); i++) {
        SaveDecision d = decide_local_save(vault, set.saves[i], (int)i, cfg, cfg_policy, dav);
        if (!d.group_key.empty()) local_gks.push_back(d.group_key);
        out.push_back(d);
    }

    if (ops.list_remote_only) {
        std::vector<std::string> remote_gks;
        int rrc = ops.list_remote_only(ops.ctx, vault, dav, title, local_gks, remote_gks);
        if (rrc != 0)
            printf("[sync] %s: remote-only probe failed (err=%d) -- continuing\n", name, rrc);
        for (size_t i = 0; i < remote_gks.size(); i++) {
            SaveDecision d;
            d.group_key = remote_gks[i];
            d.base_path = sync_base_path(vault, d.group_key);
            if (!d.base_path.empty()) decide_from_heads(vault, d, cfg_policy, cfg.device_id, dav);
            printf("[sync] %s: remote-only %s -> %s\n", name, d.group_key.c_str(),
                   d.decision_type.empty() ? "(no decision)" : d.decision_type.c_str());
            out.push_back(d);
        }
    }
    return 0;
}

static int push_save(const WsVault* vault, const LocalSave& s, const SyncEngineCfg& cfg,
                     WebDavSession* dav, const SyncProgress* prog) {
    report_step(prog, "Encrypting");
    std::string mtime, hash, group_key;
    WsBuf zip = {0, 0};
    unsigned long long t0 = now_ms();
    if (!package_local(s, mtime, hash, group_key, &zip)) {
        printf("[sync] push: packaging failed\n");
        return -1;
    }
    printf("[sync] push %s hash=%.12s mtime=%s zip=%zu bytes in %llu ms\n", group_key.c_str(),
           hash.c_str(), mtime.c_str(), (size_t)zip.len, since_ms(t0));

    std::string base_path = sync_base_path(vault, group_key);
    if (base_path.empty()) {
        ws_buf_free(zip);
        return -1;
    }
    std::string blobs_path = base_path + "/blobs";

    size_t zip_len = zip.len;
    t0 = now_ms();
    WsBuf enc = ws_vault_encrypt_blob(vault, zip.ptr, zip.len);
    ws_buf_free(zip);
    if (!enc.ptr) {
        printf("[sync] push %s: ws_vault_encrypt_blob failed: %s\n", group_key.c_str(), ffi_err());
        return -1;
    }
    printf("[sync] encrypt done: %zu -> %zu bytes in %llu ms\n", zip_len, (size_t)enc.len, since_ms(t0));

    char* bn = ws_vault_blob_name(vault, hash.c_str());
    if (!bn) {
        printf("[sync] push %s: ws_vault_blob_name failed: %s\n", group_key.c_str(), ffi_err());
        ws_buf_free(enc);
        return -1;
    }
    std::string blob_remote = blobs_path + "/" + bn + ".bin";
    ws_string_free(bn);

    report_step(prog, "Checking server");
    t0 = now_ms();
    int exists = webdav_exists_s(dav, blob_remote.c_str());
    printf("[sync] blob exists check rc=%d in %llu ms\n", exists, since_ms(t0));
    if (exists <= 0) {
        report_step(prog, "Uploading");
        if (prog && prog->bytes) prog->bytes(0, enc.len, prog->ctx);
        printf("[sync] upload blob start: %zu bytes\n", (size_t)enc.len);
        int prc = put_creating_parent(dav, blobs_path, blob_remote, enc.ptr, enc.len,
                                      "Uploading", true, prog);
        if (prc != 0) {
            printf("[sync] PUT blob failed (rc=%d)\n", prc);
            ws_buf_free(enc);
            return -1;
        }
        printf("[sync] upload blob done: %zu bytes\n", (size_t)enc.len);
    } else {
        report_step(prog, "Already up to date");
        printf("[sync] blob already on server -- upload skipped\n");
    }
    ws_buf_free(enc);

    report_step(prog, "Updating index");
    int hrc = put_device_head(vault, base_path, cfg.device_id, hash, mtime, true, dav, prog);
    printf("[sync] push %s %s\n", group_key.c_str(), hrc == 0 ? "done" : "FAILED (head)");
    return hrc;
}

int sync_restore_hash(const WsVault* vault, const void* title, const ShellOps& ops,
                      const SyncEngineCfg& cfg, const std::string& base_path,
                      const std::string& group_key, const std::string& hash,
                      const std::vector<uint8_t>& raw_tree, WebDavSession* dav,
                      const SyncProgress* prog) {
    printf("[sync] restore %s hash=%.12s safety_backup=%d\n", group_key.c_str(), hash.c_str(),
           (int)cfg.safety_backup);
    char* bn = ws_vault_blob_name(vault, hash.c_str());
    if (!bn) {
        printf("[sync] restore %s: ws_vault_blob_name failed: %s\n", group_key.c_str(), ffi_err());
        return -1;
    }
    std::string blob_remote = base_path + "/blobs/" + bn + ".bin";
    ws_string_free(bn);

    report_step(prog, "Downloading");
    std::vector<uint8_t> enc;
    unsigned long long t0 = now_ms();
    int grc = webdav_get_s(dav, blob_remote.c_str(), &enc, prog ? prog->bytes : 0,
                           prog ? prog->ctx : 0);
    if (grc == 1) {
        printf("[sync] restore %s: blob not found (404)\n", group_key.c_str());
        return -1;
    }
    if (grc != 0) {
        printf("[sync] restore %s: GET blob failed (rc=%d)\n", group_key.c_str(), grc);
        return -1;
    }
    printf("[sync] download blob done: %zu bytes in %llu ms\n", enc.size(), since_ms(t0));

    WsBuf dec = ws_vault_decrypt_blob(vault, enc.data(), enc.size());
    std::vector<uint8_t>().swap(enc);
    if (!dec.ptr) {
        printf("[sync] restore %s: ws_vault_decrypt_blob failed: %s\n", group_key.c_str(), ffi_err());
        return -1;
    }
    size_t blob_len = dec.len;
    WsBuf tree = ws_unzip(dec.ptr, dec.len);
    ws_buf_free(dec);
    if (!tree.ptr) {
        printf("[sync] restore %s: ws_unzip failed: %s\n", group_key.c_str(), ffi_err());
        return -1;
    }
    printf("[sync] unzip ok (blob %zu bytes -> file_tree %zu bytes)\n", blob_len, (size_t)tree.len);

    report_step(prog, "Restoring");
    if (!cfg.safety_backup) {
        printf("[sync] safety_backup off -- no snapshot for %s\n", group_key.c_str());
    } else if (raw_tree.empty()) {
        printf("[sync] safety snapshot skipped for %s: no local save to back up\n", group_key.c_str());
    } else {
        std::string sanitized = snapshot_sanitize_key(group_key);
        std::string backup_dir = std::string(kBackupsRoot) + sanitized + "/" + history_timestamp();
        if (!write_snapshot(backup_dir.c_str(), raw_tree.data(), raw_tree.size())) {
            printf("[sync] safety snapshot FAILED for %s -- restore skipped\n", group_key.c_str());
            ws_buf_free(tree);
            return -1;
        }
        printf("[sync] safety snapshot -> %s\n", backup_dir.c_str());
        std::string root = std::string(kBackupsRoot) + sanitized;
        if (!snapshot_prune(root.c_str(), SNAPSHOT_KEEP))
            printf("[snapshot] WARN: prune failed for %s (non-fatal)\n", group_key.c_str());
    }

    int wrc = ops.write_save(ops.ctx, title, group_key, tree.ptr, tree.len);
    ws_buf_free(tree);
    printf("[sync] restore %s write rc=%d\n", group_key.c_str(), wrc);
    return wrc == 0 ? 0 : -1;
}

int sync_pull_hash(const WsVault* vault, const void* title, const ShellOps& ops,
                   const SyncEngineCfg& cfg, const std::string& base_path,
                   const std::string& group_key, const std::string& hash,
                   const std::string& head_mtime, const std::vector<uint8_t>& raw_tree,
                   WebDavSession* dav, const SyncProgress* prog) {
    int rc = sync_restore_hash(vault, title, ops, cfg, base_path, group_key, hash, raw_tree, dav, prog);
    if (rc != 0) return rc;
    report_step(prog, "Updating index");
    std::string mtime = head_mtime.empty() ? utc_now() : head_mtime;
    if (put_device_head(vault, base_path, cfg.device_id, hash, mtime, false, dav, prog) != 0)
        printf("[sync] WARN: base update failed for %s -- healed by the next in-sync pass\n",
               group_key.c_str());
    else
        printf("[sync] base updated for %s -> %.12s @ %s\n", group_key.c_str(), hash.c_str(),
               mtime.c_str());
    return 0;
}

TitleTally sync_title(const WsVault* vault, const void* title, const char* title_name,
                      const ShellOps& ops, const SyncEngineCfg& cfg, WebDavSession* dav,
                      const SyncProgress* prog) {
    TitleTally t;
    unsigned long long t0 = now_ms();
    printf("[sync] title '%s' begin (policy=%d safety_backup=%d)\n", title_name,
           cfg.conflict_policy, (int)cfg.safety_backup);

    LocalSaveSet set;
    std::vector<SaveDecision> ds;
    if (decide_title(vault, title, title_name, ops, cfg, cfg.conflict_policy, dav, prog, set, ds) != 0) {
        tally_fail(t, "Read save failed");
        printf("[sync] title '%s' FAILED: could not read local saves -- continuing\n", title_name);
        return t;
    }
    if (ds.empty()) printf("[sync] title '%s': nothing to sync\n", title_name);

    for (size_t i = 0; i < ds.size(); i++) {
        const SaveDecision& d = ds[i];
        SyncAct act = sync_act_for(d.decision_type, d.winner);
        switch (act) {
        case SyncAct::None:
            report_step(prog, "Already up to date");
            if (base_needs_repair(d.decision_type, d.own_head_hash, d.local_hash) &&
                !d.base_path.empty() && !d.head_mtime.empty()) {
                printf("[sync] %s: base stale (own=%.12s local=%.12s) -- repairing\n",
                       d.group_key.c_str(), d.own_head_hash.c_str(), d.local_hash.c_str());
                if (put_device_head(vault, d.base_path, cfg.device_id, d.local_hash, d.head_mtime,
                                    false, dav, prog) != 0)
                    printf("[sync] WARN: base repair failed for %s (non-fatal)\n", d.group_key.c_str());
            }
            break;
        case SyncAct::Push:
            if (d.save_index < 0) {
                printf("[sync] %s: push decided for a remote-only save -- skipped\n", d.group_key.c_str());
                tally_fail(t, "Upload failed");
            } else if (push_save(vault, set.saves[(size_t)d.save_index], cfg, dav, prog) == 0) {
                t.uploaded = true;
            } else {
                printf("[sync] %s: push FAILED -- continuing\n", d.group_key.c_str());
                tally_fail(t, "Upload failed");
            }
            break;
        case SyncAct::Pull:
            if (d.pull_hash.empty()) {
                printf("[sync] %s: no pull hash -- skipped\n", d.group_key.c_str());
                tally_fail(t, "Restore failed");
            } else if (sync_pull_hash(vault, title, ops, cfg, d.base_path, d.group_key, d.pull_hash,
                                      d.head_mtime, set.raw_tree, dav, prog) == 0) {
                t.downloaded = true;
            } else {
                printf("[sync] %s: restore FAILED -- continuing\n", d.group_key.c_str());
                tally_fail(t, "Restore failed");
            }
            break;
        case SyncAct::Conflict:
            printf("[sync] %s: conflict -- skipped (resolve in Conflicts)\n", d.group_key.c_str());
            t.conflict = true;
            break;
        case SyncAct::ScanFailed:
            printf("[sync] save %zu (%s): server check failed -- continuing\n", i,
                   d.group_key.empty() ? "?" : d.group_key.c_str());
            tally_fail(t, "Server check failed");
            break;
        case SyncAct::Unknown:
            printf("[sync] %s: unknown decision '%s' -- skipped\n", d.group_key.c_str(),
                   d.decision_type.c_str());
            tally_fail(t, "Unknown decision");
            break;
        }
    }
    printf("[sync] title '%s' done in %llu ms (up=%d down=%d conflict=%d failed=%d%s%s)\n",
           title_name, since_ms(t0), (int)t.uploaded, (int)t.downloaded, (int)t.conflict,
           (int)t.failed, t.reason.empty() ? "" : " reason=", t.reason.c_str());
    return t;
}

std::vector<SaveDecision> sync_scan_title(const WsVault* vault, const void* title,
                                          const char* title_name, const ShellOps& ops,
                                          const SyncEngineCfg& cfg, int policy,
                                          WebDavSession* dav, bool* error,
                                          const SyncProgress* prog) {
    if (error) *error = false;
    LocalSaveSet set;
    std::vector<SaveDecision> ds;
    if (decide_title(vault, title, title_name, ops, cfg, policy, dav, prog, set, ds) != 0) {
        if (error) *error = true;
        return ds;
    }
    for (size_t i = 0; i < ds.size(); i++) ds[i].raw_tree = set.raw_tree;
    printf("[sync] scan '%s': %zu decision(s) (policy=%d)\n", title_name, ds.size(), policy);
    return ds;
}

int sync_push_group(const WsVault* vault, const void* title, const char* title_name,
                    const ShellOps& ops, const SyncEngineCfg& cfg,
                    const std::string& group_key, WebDavSession* dav, const SyncProgress* prog) {
    printf("[sync] keep-local '%s' %s begin\n", title_name, group_key.c_str());
    LocalSaveSet set;
    int lrc = ops.list_saves(ops.ctx, title, set);
    if (lrc != 0) {
        printf("[sync] keep-local %s: list_saves failed (err=%d)\n", group_key.c_str(), lrc);
        return -1;
    }
    for (size_t i = 0; i < set.saves.size(); i++) {
        const LocalSave& s = set.saves[i];
        std::string mtime, hash, gk;
        if (!package_local(s, mtime, hash, gk, 0)) {
            printf("[sync] keep-local save %zu: packaging failed -- skipped\n", i);
            continue;
        }
        if (gk != group_key) continue;
        int rc = push_save(vault, s, cfg, dav, prog);
        printf("[sync] keep-local %s %s\n", group_key.c_str(), rc == 0 ? "done" : "FAILED");
        return rc;
    }
    printf("[sync] keep-local %s: no local save matches (deleted since scan?)\n", group_key.c_str());
    return -1;
}

int sync_local_keys(const WsVault* vault, const void* title, const ShellOps& ops,
                    std::vector<LocalSaveKey>& out, std::vector<uint8_t>* raw_tree) {
    out.clear();
    if (raw_tree) raw_tree->clear();
    unsigned long long t0 = now_ms();
    LocalSaveSet set;
    int lrc = ops.list_saves(ops.ctx, title, set);
    if (lrc != 0) {
        printf("[sync] local keys: list_saves failed (err=%d)\n", lrc);
        return -1;
    }
    for (size_t i = 0; i < set.saves.size(); i++) {
        std::string mtime, hash, gk;
        if (!package_local(set.saves[i], mtime, hash, gk, 0)) {
            printf("[sync] local keys: save %zu packaging failed -- skipped\n", i);
            continue;
        }
        LocalSaveKey k;
        k.group_key = gk;
        k.base_path = sync_base_path(vault, gk);
        printf("[sync] local keys: save %zu group_key=%s base_path=%s\n", i, gk.c_str(),
               k.base_path.empty() ? "(empty)" : k.base_path.c_str());
        out.push_back(k);
    }
    const size_t raw_len = set.raw_tree.size();
    if (raw_tree) raw_tree->swap(set.raw_tree);
    printf("[sync] local keys: %zu key(s) from %zu save(s), raw tree %zu bytes in %llu ms\n",
           out.size(), set.saves.size(), raw_len, since_ms(t0));
    return (int)set.saves.size();
}
