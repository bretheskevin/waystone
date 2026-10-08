#include "update_worker.h"
#include "worker_thread.h"
#include <cstdio>

UpdateWorker::UpdateWorker()
    : phase_((int)UpdatePhase::Idle),
      running_(false),
      cancel_(false),
      progress_(0),
      check_rc_(UP_OK),
      install_rc_(UP_OK),
      thread_(0)
{
    LightLock_Init(&mu_);
}

UpdateWorker::~UpdateWorker() {
    join();
}

void UpdateWorker::join() {
    if (thread_) {
        threadJoin(thread_, U64_MAX);
        threadFree(thread_);
        thread_ = 0;
    }
}

void UpdateWorker::request_cancel() {
    cancel_.store(true);
}

UpdatePhase UpdateWorker::phase() const {
    return (UpdatePhase)phase_.load();
}

int UpdateWorker::progress_percent() {
    return progress_.load();
}

std::string UpdateWorker::latest_version() {
    LightLock_Lock(&mu_);
    std::string s = latest_ver_;
    LightLock_Unlock(&mu_);
    return s;
}

std::string UpdateWorker::asset_url() {
    LightLock_Lock(&mu_);
    std::string s = asset_url_;
    LightLock_Unlock(&mu_);
    return s;
}

// ---- check ----

void UpdateWorker::check_entry(void* arg) {
    static_cast<UpdateWorker*>(arg)->check_worker();
}

void UpdateWorker::start_check() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    join();
    phase_.store((int)UpdatePhase::Checking);
    progress_.store(0);
    cancel_.store(false);
    check_rc_.store(UP_OK);
    {
        LightLock_Lock(&mu_);
        latest_ver_.clear();
        asset_url_.clear();
        LightLock_Unlock(&mu_);
    }
    thread_ = start_worker_thread(check_entry, this);
    if (!thread_) {
        printf("[update] threadCreate failed (check)\n");
        phase_.store((int)UpdatePhase::Error);
        check_rc_.store(UP_NET);
        running_.store(false);
    }
}

void UpdateWorker::check_worker() {
    printf("[update] worker: checking latest release\n");
    char ver[64];
    char url[768];
    int rc = updater_check_latest(".3dsx", ver, sizeof(ver), url, sizeof(url));
    check_rc_.store(rc);
    if (rc == UP_OK) {
        LightLock_Lock(&mu_);
        latest_ver_ = ver;
        asset_url_ = url;
        LightLock_Unlock(&mu_);
        phase_.store((int)UpdatePhase::Done);
        printf("[update] worker: check done, latest=v%s\n", ver);
    } else {
        phase_.store((int)UpdatePhase::Error);
        printf("[update] worker: check FAILED rc=%d\n", rc);
    }
    running_.store(false);
}

// ---- install ----

void UpdateWorker::install_entry(void* arg) {
    static_cast<UpdateWorker*>(arg)->install_worker();
}

void UpdateWorker::start_install(const std::string& url) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    join();
    {
        LightLock_Lock(&mu_);
        install_url_ = url;
        LightLock_Unlock(&mu_);
    }
    phase_.store((int)UpdatePhase::Downloading);
    progress_.store(0);
    cancel_.store(false);
    install_rc_.store(UP_OK);
    thread_ = start_worker_thread(install_entry, this);
    if (!thread_) {
        printf("[update] threadCreate failed (install)\n");
        phase_.store((int)UpdatePhase::Error);
        install_rc_.store(UP_IO);
        running_.store(false);
    }
}

bool UpdateWorker::download_progress(size_t got, size_t total, void* ctx) {
    auto* self = static_cast<UpdateWorker*>(ctx);
    if (self->cancel_.load()) {
        printf("[update] download cancelled by user\n");
        return false;  // aborts the curl transfer
    }
    int pct = (total > 0) ? static_cast<int>(got * 100 / total) : 0;
    if (pct > 100) pct = 100;
    self->progress_.store(pct);
    return true;
}

void UpdateWorker::install_worker() {
    char self_path[768];
    int rc = updater_self_path(self_path, sizeof(self_path));
    if (rc != UP_OK) {
        install_rc_.store(rc);
        phase_.store((int)UpdatePhase::Error);
        running_.store(false);
        return;
    }

    std::string url;
    {
        LightLock_Lock(&mu_);
        url = install_url_;
        LightLock_Unlock(&mu_);
    }

    printf("[update] worker: installing from %.48s\n", url.c_str());
    rc = updater_install(url.c_str(), self_path, download_progress, this);
    if (rc != UP_OK) {
        install_rc_.store(rc);
        progress_.store(0);
        phase_.store((int)UpdatePhase::Error);
        printf("[update] worker: install FAILED rc=%d\n", rc);
        running_.store(false);
        return;
    }

    progress_.store(100);
    phase_.store((int)UpdatePhase::Done);
    printf("[update] worker: install complete\n");
    running_.store(false);
}
