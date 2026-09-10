#include "net_status.h"
#include <switch.h>

bool network_available() {
    if (R_FAILED(nifmInitialize(NifmServiceType_User)))
        return false;
    NifmInternetConnectionType conn_type;
    u32 wifi_strength;
    NifmInternetConnectionStatus conn_status;
    Result rc = nifmGetInternetConnectionStatus(&conn_type, &wifi_strength, &conn_status);
    nifmExit();
    if (R_FAILED(rc))
        return false;
    return conn_status == NifmInternetConnectionStatus_Connected;
}
