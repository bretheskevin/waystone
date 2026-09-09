/*
 * Preview stubs for shell-common/wsconfig.h.
 * wsconfig_save is called in the real vault_helpers.cpp, but we provide a
 * stub vault_helpers_stub.cpp that doesn't call it.  These stubs satisfy the
 * linker anyway in case something else references the symbols.
 */
#include "wsconfig.h"

WaystoneShellConfig wsconfig_load(const char* /*path*/)
{
    return WaystoneShellConfig{};
}

bool wsconfig_save(const WaystoneShellConfig& /*cfg*/, const char* /*path*/)
{
    return true;
}
