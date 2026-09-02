#include "sync.h"
#include "json.h"

#include <cstdio>
#include <cstring>
#include <ctime>

extern "C" {
#include "waystone.h"
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

        // Split group_key ("system/game_key/slot") into path segments
        // Then obfuscate each segment through the vault.
        std::string sys_str, game_str, slot_str;
        {
            size_t p1 = group_key.find('/');
            size_t p2 = (p1 != std::string::npos) ? group_key.find('/', p1 + 1)
                                                   : std::string::npos;
            if (p1 == std::string::npos || p2 == std::string::npos) {
                printf("FAIL: malformed group_key\n");
                ws_buf_free(zip);
                continue;
            }
            sys_str = group_key.substr(0, p1);
            game_str = group_key.substr(p1 + 1, p2 - p1 - 1);
            slot_str = group_key.substr(p2 + 1);
        }

        char* sys_seg = ws_vault_path_segment(vault, sys_str.c_str());
        char* game_seg = ws_vault_path_segment(vault, game_str.c_str());
        char* slot_seg = ws_vault_path_segment(vault, slot_str.c_str());
        if (!sys_seg || !game_seg || !slot_seg) {
            printf("FAIL: ws_vault_path_segment returned null\n");
            ws_string_free(sys_seg);
            ws_string_free(game_seg);
            ws_string_free(slot_seg);
            ws_buf_free(zip);
            continue;
        }

        std::string base_path = std::string(sys_seg) + "/" + game_seg + "/" + slot_seg;
        ws_string_free(sys_seg);
        ws_string_free(game_seg);
        ws_string_free(slot_seg);

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
