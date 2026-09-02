#ifndef WAYSTONE_SYNC_H
#define WAYSTONE_SYNC_H

#include "net.h"
#include "saves.h"

struct Vault;
typedef Vault WsVault;

// Push all saves for a single title to the WebDAV backend.
// Steps per save:
//   1. ws_jksv_normalize -> array of NormalizedSaveDto
//   2. For each: set mtime, ws_package -> SaveEntry + zip
//   3. ws_vault_encrypt_blob -> encrypted blob
//   4. ws_vault_blob_name -> obfuscated blob name
//   5. ws_vault_path_segment -> obfuscated path segments
//   6. webdav_mkdir_p + webdav_put (blob, head, history)
// Returns number of saves pushed (0 means nothing to push, -1 means error).
int push_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav);

// Pull/restore saves for a single title from the WebDAV backend.
// Mirrors desktop do_pull_save pipeline, per normalized local save:
//   1. Package local save -> local_hash + group_key -> base_path
//   2. PROPFIND {base_path}/heads -> decrypt each .json -> heads array
//   3. ws_decide_pull -> determine pull_hash (or skip)
//   4. If pull: GET+decrypt blob -> ws_unzip -> write_save_files
// Returns number of saves restored (0 = nothing to do, -1 = error).
int pull_title(const WsVault* vault,
               const TitleInfo& title,
               AccountUid uid,
               const char* device_id,
               const WebDavCfg& dav);

#endif
