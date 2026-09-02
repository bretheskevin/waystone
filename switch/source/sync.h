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

#endif
