/*
 * Preview stub for switch/source/ui/update_controller.cpp (linked INSTEAD of it) plus the
 * updater.cpp / libnx env symbols SettingsPane references. No thread, no network: a check
 * finishes immediately with a canned newer release, an install succeeds immediately.
 */
#include "update_controller.h"
#include <switch.h>
#include <cstdio>
#include <cstring>

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

// Preview only: any version other than the running one counts as newer.
bool version_newer(const char* latest, const char* current) { return std::strcmp(latest, current) != 0; }

int updater_self_path(char*, size_t) { return UP_NO_SELF; }
std::string updater_check_message(int rc, const char*) { return "Update check failed (" + std::to_string(rc) + ")"; }
std::string updater_install_message(int rc) { return "Update failed (" + std::to_string(rc) + ")"; }

extern "C" bool envHasNextLoad(void) { return false; }
extern "C" Result envSetNextLoad(const char*, const char*) { return 1; }
