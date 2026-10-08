#pragma once
#include <atomic>
#include <string>
#include <3ds.h>
#include "updater.h"

enum class UpdatePhase { Idle, Checking, Downloading, Installing, Done, Error };

// Owns the background check/download for in-app self-updates. One worker lives
// for the SettingsScreen lifetime; the screen polls phase()/status()/progress.
// Two modes, fixed at construction: .3dsx (HBL/netload: replace the .3dsx file) and
// CIA (installed title: download the .cia and import it through AM).
class UpdateWorker {
public:
    UpdateWorker();
    ~UpdateWorker();

    void start_check();
    void start_install(const std::string& url);
    void request_cancel();  // aborts an in-flight download/CIA import (progress cb returns false)
    void join();
    bool is_running() const { return running_.load(); }

    bool cia_mode() const { return cia_mode_; }
    const char* asset_suffix() const { return cia_mode_ ? ".cia" : ".3dsx"; }

    UpdatePhase phase() const;
    int progress_percent();

    // Result of the last start_check(): rc + (on UP_OK) the discovered version
    // and asset URL. Valid once phase() is Done/Error after a check.
    int check_rc() const { return check_rc_.load(); }
    std::string latest_version();
    std::string asset_url();

    // Result of the last start_install() (an UpdaterRc).
    int install_rc() const { return install_rc_.load(); }

private:
    bool cia_mode_;
    std::atomic<int> phase_;
    std::atomic<bool> running_;
    std::atomic<bool> cancel_;
    std::atomic<int> progress_;    // 0..100 during Downloading / Installing
    std::atomic<int> check_rc_;
    std::atomic<int> install_rc_;
    LightLock mu_;
    std::string latest_ver_;  // guarded by mu_
    std::string asset_url_;   // guarded by mu_
    std::string install_url_; // guarded by mu_ (copied to worker at start)
    Thread thread_;

    static void check_entry(void* arg);
    void check_worker();
    static void install_entry(void* arg);
    void install_worker();
    void install_cia(const std::string& url);
    void finish_install(int rc);
    static bool transfer_progress(size_t got, size_t total, void* ctx);

    UpdateWorker(const UpdateWorker&);
    UpdateWorker& operator=(const UpdateWorker&);
};
