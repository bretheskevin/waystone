#include "net_status.h"
#include <3ds.h>

// ACU_GetStatus returns: 1 = not connected, 3 = connected (internet).
// Mirror the Switch's NIFM pattern: per-call init/exit, return false on any failure.
bool network_available() {
    if (R_FAILED(acInit()))
        return false;
    u32 status = 0;
    Result rc = ACU_GetStatus(&status);
    acExit();
    return R_SUCCEEDED(rc) && status == 3;
}
