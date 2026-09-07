#ifndef WAYSTONE_3DS_SYNC_H
#define WAYSTONE_3DS_SYNC_H

#include "net.h"
#include "saves.h"

struct Vault;
typedef Vault WsVault;

// Push all saves for a single title to the WebDAV backend.
// 3DS savedata is per-title (no AccountUid).
// Returns number of saves pushed (0 = nothing to push, -1 = error).
int push_title(const WsVault* vault,
               const TitleInfo& title,
               const char* device_id,
               const WebDavCfg& dav);

#endif
