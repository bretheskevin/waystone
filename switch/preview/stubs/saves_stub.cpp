/*
 * Preview stubs for switch/source/saves.h.
 * session.h includes saves.h, which includes <switch.h> and declares
 * list_titles(), get_active_account(), etc.
 * These are never called in the wizard UI layer, but the linker needs the symbols.
 */
#include "saves.h"
#include <string>
#include <vector>

std::vector<TitleInfo> list_titles()
{
    return {};
}

std::vector<uint8_t> extract_save_json(const TitleInfo& /*title*/, AccountUid /*uid*/, std::string* /*local_mtime*/)
{
    return {};
}

bool get_active_account(AccountUid* out_uid)
{
    if (out_uid) *out_uid = {};
    return true;
}

std::string current_utc_time()
{
    return "2026-01-01T00:00:00Z";
}

std::string get_device_id()
{
    return "preview-device-0000000000000000";
}

int write_save_files(u64 /*title_id*/, AccountUid /*uid*/, const uint8_t* /*ft_ptr*/, size_t /*ft_len*/)
{
    return 0;
}
