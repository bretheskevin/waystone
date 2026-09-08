#include "sync.h"
#include "json.h"
#include "snapshot.h"

#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
#include "waystone.h"
}

// Compute the obfuscated remote base_path from a group_key ("system/game/slot").
// Returns empty string on failure.
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

// Helper: format a timestamp suitable for history filenames (20260902T153000Z).
static std::string history_timestamp() {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &t);
    return buf;
}

SaveDecision scan_save_decision(const WsVault* vault, const char* save_json,
                                const char* mtime, const char* device_id,
                                int policy, const WebDavCfg& dav,
                                const std::string& raw_json) {
    SaveDecision d;
    d.raw_json = raw_json;

    WsBuf zip = {nullptr, 0};
    char* entry_json = ws_package(save_json, &zip);
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

    std::string heads_path = d.base_path + "/heads";
    std::vector<std::string> hrefs;
    if (webdav_propfind(dav, heads_path.c_str(), &hrefs) != 0) {
        printf("FAIL: PROPFIND %s\n", heads_path.c_str());
        return d;
    }
    if (hrefs.empty()) { printf("no remote heads found -- skipping\n"); d.decision_type = "in_sync"; return d; }

    d.heads_array = "[";
    bool first_head = true;
    for (const auto& href : hrefs) {
        if (href.size() < 5 || href.compare(href.size() - 5, 5, ".json") != 0) continue;
        std::vector<uint8_t> enc_data;
        if (webdav_get(dav, href.c_str(), &enc_data) != 0) continue;
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
    if (first_head) { printf("no decryptable heads -- skipping\n"); d.decision_type = "in_sync"; return d; }

    const char* local_hash_ptr = d.local_hash.empty() ? nullptr : d.local_hash.c_str();
    char* decision_json = ws_decide_pull(local_hash_ptr, mtime, d.heads_array.c_str(), device_id, policy);
    if (!decision_json) {
        printf("FAIL ws_decide_pull: %s\n", ws_last_error() ? ws_last_error() : "unknown");
        return d;
    }
    d.decision_type = json_get_string(decision_json, "type");
    if (d.decision_type == "pull") {
        d.pull_hash = json_get_string(decision_json, "head_hash");
    } else if (d.decision_type == "conflict_resolved") {
        d.winner = json_get_string(decision_json, "winner");
        if (d.winner == "remote") {
            char* folded = ws_fold_heads(d.heads_array.c_str());
            if (folded) { d.pull_hash = json_get_string(folded, "hash"); ws_string_free(folded); }
        }
    }
    ws_string_free(decision_json);
    return d;
}

int restore_remote_save(const WsVault* vault, const std::string& pull_hash,
                        const std::string& base_path, const std::string& group_key,
                        const std::string& raw_json, u64 title_id,
                        AccountUid uid, const WebDavCfg& dav) {
    char* blob_name = ws_vault_blob_name(vault, pull_hash.c_str());
    if (!blob_name) { printf("  FAIL: ws_vault_blob_name: %s\n", ws_last_error() ? ws_last_error() : "unknown"); return -1; }
    std::string blob_remote = base_path + "/blobs/" + blob_name + ".bin";
    ws_string_free(blob_name);

    std::vector<uint8_t> enc_blob;
    int grc = webdav_get(dav, blob_remote.c_str(), &enc_blob);
    if (grc == 1) { printf("  FAIL: blob not found (404)\n"); return -1; }
    else if (grc != 0) { printf("  FAIL: GET blob error\n"); return -1; }

    WsBuf decrypted_blob = ws_vault_decrypt_blob(vault, enc_blob.data(), enc_blob.size());
    if (!decrypted_blob.ptr) { printf("  FAIL: ws_vault_decrypt_blob: %s\n", ws_last_error() ? ws_last_error() : "unknown"); return -1; }

    char* files_json = ws_unzip(decrypted_blob.ptr, decrypted_blob.len);
    ws_buf_free(decrypted_blob);
    if (!files_json) { printf("  FAIL: ws_unzip: %s\n", ws_last_error() ? ws_last_error() : "unknown"); return -1; }

    {
        std::string sanitized = snapshot_sanitize_key(group_key);
        std::string snap_ts = history_timestamp();
        std::string backup_dir = std::string("sdmc:/waystone/backups/") + sanitized + "/" + snap_ts;
        if (!write_snapshot(backup_dir.c_str(), raw_json.c_str())) {
            printf("  WARN: safety snapshot failed for %s, skipping restore\n", group_key.c_str());
            ws_string_free(files_json);
            return -1;
        }
    }

    int wrc = write_save_files(title_id, uid, files_json);
    ws_string_free(files_json);
    return wrc;
}

int push_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav) {

    printf("  Extracting save data...\n");
    std::string raw_json = extract_save_json(title, uid);
    if (raw_json.empty()) {
        printf("  No save data found.\n");
        return 0;
    }

    // Normalize through the JKSV adapter
    printf("  Normalizing...\n");
    char* norm_json = ws_jksv_normalize("switch", raw_json.c_str());
    if (!norm_json) {
        printf("  ws_jksv_normalize failed: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        return -1;
    }

    // Split the array of NormalizedSaveDto into individual elements
    std::vector<std::string> saves = json_split_array(norm_json);
    ws_string_free(norm_json);

    if (saves.empty()) {
        printf("  No normalized saves produced.\n");
        return 0;
    }

    std::string mtime = current_utc_time();
    int pushed = 0;

    for (size_t si = 0; si < saves.size(); si++) {
        printf("  Save %zu/%zu: ", si + 1, saves.size());

        // Inject mtime into the NormalizedSaveDto (adapter leaves it empty)
        std::string save_json = json_set_mtime(saves[si], mtime.c_str());

        // Package: NormalizedSaveDto -> SaveEntry + zip bytes
        WsBuf zip = {nullptr, 0};
        char* entry_json = ws_package(save_json.c_str(), &zip);
        if (!entry_json) {
            printf("FAIL ws_package: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            continue;
        }

        // Parse SaveEntry for content.hash and group_key
        std::string content_hash = json_get_nested_string(entry_json, "content", "hash");
        std::string group_key = json_get_string(entry_json, "group_key");
        ws_string_free(entry_json);

        if (content_hash.empty() || group_key.empty()) {
            printf("FAIL: could not parse SaveEntry\n");
            ws_buf_free(zip);
            continue;
        }

        printf("group=%s hash=%s\n", group_key.c_str(),
               content_hash.substr(0, 12).c_str());

        std::string base_path = make_base_path(vault, group_key);
        if (base_path.empty()) {
            ws_buf_free(zip);
            continue;
        }

        // Create remote directory structure
        std::string blobs_path = base_path + "/blobs";
        std::string heads_path = base_path + "/heads";
        std::string history_path = base_path + "/history";
        if (webdav_mkdir_p(dav, blobs_path.c_str()) != 0 ||
            webdav_mkdir_p(dav, heads_path.c_str()) != 0 ||
            webdav_mkdir_p(dav, history_path.c_str()) != 0) {
            printf("FAIL: mkdir_p\n");
            ws_buf_free(zip);
            continue;
        }

        // Encrypt the save blob
        WsBuf encrypted = ws_vault_encrypt_blob(vault, zip.ptr, zip.len);
        ws_buf_free(zip);
        if (!encrypted.ptr) {
            printf("FAIL: ws_vault_encrypt_blob: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            continue;
        }

        // Upload blob (skip if already exists)
        char* blob_name = ws_vault_blob_name(vault, content_hash.c_str());
        if (!blob_name) {
            printf("FAIL: ws_vault_blob_name\n");
            ws_buf_free(encrypted);
            continue;
        }
        std::string blob_remote = blobs_path + "/" + blob_name + ".bin";
        ws_string_free(blob_name);

        int exists = webdav_exists(dav, blob_remote.c_str());
        if (exists <= 0) {
            // Does not exist (or error checking) -- upload
            if (webdav_put(dav, blob_remote.c_str(), encrypted.ptr, encrypted.len) != 0) {
                printf("FAIL: PUT blob\n");
                ws_buf_free(encrypted);
                continue;
            }
        }
        ws_buf_free(encrypted);

        // Build and encrypt the DeviceHead JSON
        std::string head_json = build_device_head_json(
            device_id, content_hash.c_str(), mtime.c_str());
        WsBuf encrypted_head = ws_vault_encrypt_heads(
            vault,
            reinterpret_cast<const uint8_t*>(head_json.data()),
            head_json.size());
        if (!encrypted_head.ptr) {
            printf("FAIL: ws_vault_encrypt_heads: %s\n",
                   ws_last_error() ? ws_last_error() : "unknown");
            continue;
        }

        // Upload head
        std::string head_remote = heads_path + "/" + device_id + ".json";
        if (webdav_put(dav, head_remote.c_str(),
                       encrypted_head.ptr, encrypted_head.len) != 0) {
            printf("FAIL: PUT head\n");
            ws_buf_free(encrypted_head);
            continue;
        }

        // Upload history entry (re-encrypt for distinct ciphertext)
        std::string ts = history_timestamp();
        std::string hist_remote = history_path + "/" + ts + "-" + device_id + ".json";
        WsBuf encrypted_hist = ws_vault_encrypt_heads(
            vault,
            reinterpret_cast<const uint8_t*>(head_json.data()),
            head_json.size());
        if (encrypted_hist.ptr) {
            webdav_put(dav, hist_remote.c_str(),
                       encrypted_hist.ptr, encrypted_hist.len);
            ws_buf_free(encrypted_hist);
        }
        ws_buf_free(encrypted_head);

        pushed++;
        printf("  Pushed OK.\n");
    }

    return pushed;
}

int pull_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav) {

    printf("  Extracting local save data for pull comparison...\n");
    std::string raw_json = extract_save_json(title, uid);
    if (raw_json.empty()) {
        printf("  No local save data found — cannot determine remote path. Skipping.\n");
        return 0;
    }

    char* norm_json = ws_jksv_normalize("switch", raw_json.c_str());
    if (!norm_json) {
        printf("  ws_jksv_normalize failed: %s\n",
               ws_last_error() ? ws_last_error() : "unknown");
        return -1;
    }

    std::vector<std::string> saves = json_split_array(norm_json);
    ws_string_free(norm_json);

    if (saves.empty()) {
        printf("  No normalized saves produced.\n");
        return 0;
    }

    // Use a single mtime for all saves in this pull pass (mirrors push).
    std::string mtime = current_utc_time();
    int pulled = 0;

    for (size_t si = 0; si < saves.size(); si++) {
        printf("  Pull %zu/%zu: ", si + 1, saves.size());
        std::string save_json = json_set_mtime(saves[si], mtime.c_str());
        SaveDecision d = scan_save_decision(vault, save_json.c_str(), mtime.c_str(),
                                            device_id, 0 /* NewestWins */, dav, raw_json);
        if (d.decision_type == "in_sync") { printf("in_sync\n"); continue; }
        else if (d.decision_type == "push") { printf("decision=push, no pull needed\n"); continue; }
        else if (d.decision_type == "conflict_needs_input") { printf("conflict_needs_input -- manual resolution required (skipping)\n"); continue; }
        else if (d.decision_type == "conflict_resolved" && d.winner == "local") { printf("conflict_resolved winner=local, no pull needed\n"); continue; }
        else if (d.decision_type.empty()) { continue; }
        if (d.pull_hash.empty()) { printf("FAIL: could not determine pull hash\n"); continue; }
        printf("pulling hash=%.12s...\n", d.pull_hash.c_str());
        int rc = restore_remote_save(vault, d.pull_hash, d.base_path, d.group_key, d.raw_json, title.title_id, uid, dav);
        if (rc == 0) { printf("  Pulled OK.\n"); pulled++; } else { printf("  WARN: restore failure (%d)\n", rc); }
    }

    return pulled;
}
