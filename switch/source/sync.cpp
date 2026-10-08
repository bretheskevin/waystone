#include "sync.h"

#include <cstdio>

struct Vault;
extern "C" {
#include "waystone.h"
}

static const char* ffi_err() { return ws_last_error() ? ws_last_error() : "unknown"; }

static int nx_list_saves(void* ctx, const void* tp, LocalSaveSet& out) {
    const AccountUid* uid = static_cast<const AccountUid*>(ctx);
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
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

static int nx_write_save(void* ctx, const void* tp, const std::string& group_key,
                         const uint8_t* tree, size_t tree_len) {
    const AccountUid* uid = static_cast<const AccountUid*>(ctx);
    const TitleInfo& title = *static_cast<const TitleInfo*>(tp);
    printf("[saves] write %s -> tid=%016lX (%zu bytes)\n", group_key.c_str(),
           (unsigned long)title.title_id, tree_len);
    int rc = write_save_files(title.title_id, *uid, tree, tree_len);
    printf("[saves] write %s rc=%d\n", group_key.c_str(), rc);
    return rc;
}

ShellOps nx_shell_ops(AccountUid* uid) {
    ShellOps ops;
    ops.list_saves = nx_list_saves;
    ops.list_remote_only = nullptr;
    ops.write_save = nx_write_save;
    ops.ctx = uid;
    return ops;
}
