/*
 * Preview stub for switch/source/ui/update_controller.cpp (linked INSTEAD of it) plus the
 * libnx env symbols UpdateFlow references (the real shell-common/updater.cpp is compiled, its
 * network calls are stubbed in updater_net_stub.cpp). No thread, no network: a check finishes
 * immediately with a canned newer release, an install succeeds immediately.
 */
#include "update_controller.h"
#include <switch.h>
#include <cstdio>

static const char* PREVIEW_LATEST = "9.9.9";

UpdateController::~UpdateController() {}

void UpdateController::start_check() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        latest_ver_ = PREVIEW_LATEST;
        asset_url_ = "https://example.invalid/waystone.nro";
    }
    check_rc_.store(UP_OK);
    phase_.store(UpdatePhase::Done);
    printf("[update] preview stub: check -> v%s\n", PREVIEW_LATEST);
}

void UpdateController::start_install(const std::string& url) {
    install_rc_.store(UP_OK);
    phase_.store(UpdatePhase::Done);
    printf("[update] preview stub: install %s -> ok\n", url.c_str());
}

extern "C" bool envHasNextLoad(void) { return false; }
extern "C" Result envSetNextLoad(const char*, const char*) { return 1; }
