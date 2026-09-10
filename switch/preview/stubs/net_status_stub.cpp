/*
 * Preview stub for switch/source/net_status.h.
 * Always reports network as available so the preview harness can exercise
 * NoInternetActivity directly (by pushing it explicitly) without the stub
 * interfering with normal routing.
 */
#include "net_status.h"

bool network_available() {
    return true;
}
