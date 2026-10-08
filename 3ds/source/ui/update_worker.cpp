#include "update_worker.h"
#include "worker_thread.h"
#include "cia_install.h"
#include "net.h"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

static const char* const CIA_TMP_DIR  = "sdmc:/waystone";
static const char* const CIA_TMP_PATH = "sdmc:/waystone/update.cia";

UpdateWorker::UpdateWorker()
    : cia_mode_(!envIsHomebrew()),
      phase_((int)UpdatePhase::Idle),
      running_(false),
      cancel_(false),
      progress_(0),
      check_rc_(UP_OK),
      install_rc_(UP_OK),
      thread_(0)
{
    LightLock_Init(&mu_);
    printf("[update] worker created mode=%s (envIsHomebrew=%d)\n",
           cia_mode_ ? "cia" : "3dsx", cia_mode_ ? 0 : 1);
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
    printf("[update] worker: checking latest release (asset %s)\n", asset_suffix());
    char ver[64];
    char url[768];
    int rc = updater_check_latest(asset_suffix(), ver, sizeof(ver), url, sizeof(url));
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

bool UpdateWorker::transfer_progress(size_t got, size_t total, void* ctx) {
    auto* self = static_cast<UpdateWorker*>(ctx);
    if (self->cancel_.load()) {
        printf("[update] transfer cancelled by user\n");
        return false;  // aborts the curl transfer / AM import
    }
    int pct = (total > 0) ? static_cast<int>((unsigned long long)got * 100 / total) : 0;
    if (pct > 100) pct = 100;
    self->progress_.store(pct);
    return true;
}

void UpdateWorker::finish_install(int rc) {
    install_rc_.store(rc);
    if (rc == UP_OK) {
        progress_.store(100);
        phase_.store((int)UpdatePhase::Done);
        printf("[update] worker: install complete\n");
    } else {
        progress_.store(0);
        phase_.store((int)UpdatePhase::Error);
        printf("[update] worker: install FAILED rc=%d\n", rc);
    }
    running_.store(false);  // last: reap_worker deletes us once this flips
}

void UpdateWorker::install_cia(const std::string& url) {
    printf("[update] worker: cia install from %.48s -> %s\n", url.c_str(), CIA_TMP_PATH);
    if (mkdir(CIA_TMP_DIR, 0755) != 0 && errno != EEXIST) {
        printf("[update] worker: mkdir %s FAILED errno=%d\n", CIA_TMP_DIR, errno);
    }
    remove(CIA_TMP_PATH);  // stale leftover from an interrupted run

    int drc = http_download(url.c_str(), CIA_TMP_PATH, transfer_progress, this);
    if (drc != 0) {
        // http_download already removed the temp file on any failure (documented contract).
        printf("[update] worker: cia download FAILED rc=%d (installed title untouched)\n", drc);
        finish_install(UP_NET);
        return;
    }
    printf("[update] worker: cia download done, importing via AM\n");

    progress_.store(0);
    phase_.store((int)UpdatePhase::Installing);
    int irc = cia_install_file(CIA_TMP_PATH, transfer_progress, this);
    printf("[update] worker: cia import %s\n", irc == 0 ? "ok" : "FAILED");

    if (remove(CIA_TMP_PATH) == 0) {
        printf("[update] worker: removed %s\n", CIA_TMP_PATH);
    } else {
        printf("[update] worker: remove %s FAILED errno=%d\n", CIA_TMP_PATH, errno);
    }
    finish_install(irc == 0 ? UP_OK : UP_INSTALL);
}

void UpdateWorker::install_worker() {
    std::string url;
    {
        LightLock_Lock(&mu_);
        url = install_url_;
        LightLock_Unlock(&mu_);
    }

    if (cia_mode_) {
        install_cia(url);
        return;
    }

    char self_path[768];
    int rc = updater_self_path(self_path, sizeof(self_path));
    if (rc != UP_OK) {
        printf("[update] worker: no self path rc=%d\n", rc);
        finish_install(rc);
        return;
    }
    printf("[update] worker: installing from %.48s\n", url.c_str());
    finish_install(updater_install(url.c_str(), self_path, transfer_progress, this));
}
