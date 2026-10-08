#include "update_controller.h"
#include <cstdio>

UpdateController::~UpdateController() { if (thread_.joinable()) thread_.join(); }

void UpdateController::start_check() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) { printf("[update] check ignored: busy\n"); return; }
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    phase_.store(UpdatePhase::Checking);
    printf("[update] check start\n");
    thread_ = std::thread(&UpdateController::check_worker, this);
}

void UpdateController::check_worker() {
    char ver[64] = {0}, url[768] = {0};
    int rc = updater_check_latest(".nro", ver, sizeof(ver), url, sizeof(url));
    check_rc_.store(rc);
    if (rc == UP_OK) {
        std::lock_guard<std::mutex> lk(mu_);
        latest_ver_ = ver;
        asset_url_ = url;
    }
    printf("[update] check done rc=%d ver=%s\n", rc, rc == UP_OK ? ver : "-");
    phase_.store(rc == UP_OK ? UpdatePhase::Done : UpdatePhase::Error);
    running_.store(false);
}

void UpdateController::start_install(const std::string& url) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) { printf("[update] install ignored: busy\n"); return; }
    if (thread_.joinable()) thread_.join();
    cancel_.store(false);
    progress_.store(0);
    phase_.store(UpdatePhase::Downloading);
    printf("[update] install start url=%.48s\n", url.c_str());
    thread_ = std::thread(&UpdateController::install_worker, this, url);
}

bool UpdateController::download_progress(size_t got, size_t total, void* ctx) {
    auto* self = static_cast<UpdateController*>(ctx);
    if (total > 0) self->progress_.store((int)((got * 100) / total));
    return !self->cancel_.load();
}

void UpdateController::install_worker(std::string url) {
    char self_path[512] = {0};
    int rc = updater_self_path(self_path, sizeof(self_path));
    printf("[update] self path rc=%d path=%s\n", rc, rc == UP_OK ? self_path : "-");
    if (rc == UP_OK) rc = updater_install(url.c_str(), self_path, download_progress, this);
    install_rc_.store(rc);
    printf("[update] install done rc=%d (cancelled=%d)\n", rc, (int)cancel_.load());
    phase_.store(rc == UP_OK ? UpdatePhase::Done : UpdatePhase::Error);
    running_.store(false);
}
