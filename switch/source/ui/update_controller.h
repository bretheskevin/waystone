#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "updater.h"

enum class UpdatePhase { Idle, Checking, Downloading, Done, Error };

// Background GitHub check + .nro download. Never touches borealis; Settings polls it.
class UpdateController {
public:
    UpdateController() = default;
    ~UpdateController();  // joins: only deleted via reap_worker once !is_running()
    UpdateController(const UpdateController&) = delete;
    UpdateController& operator=(const UpdateController&) = delete;

    void start_check();
    void start_install(const std::string& url);
    void request_cancel() { cancel_.store(true); }
    bool is_running() const { return running_.load(); }

    UpdatePhase phase() const { return phase_.load(); }
    int progress_percent() const { return progress_.load(); }
    int check_rc() const { return check_rc_.load(); }
    int install_rc() const { return install_rc_.load(); }
    bool cancelled() const { return cancel_.load(); }
    std::string latest_version() const { std::lock_guard<std::mutex> lk(mu_); return latest_ver_; }
    std::string asset_url() const { std::lock_guard<std::mutex> lk(mu_); return asset_url_; }

private:
    std::atomic<UpdatePhase> phase_{UpdatePhase::Idle};
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<int> progress_{0};
    std::atomic<int> check_rc_{UP_OK};
    std::atomic<int> install_rc_{UP_OK};
    mutable std::mutex mu_;
    std::string latest_ver_;
    std::string asset_url_;
    std::thread thread_;

    void check_worker();
    void install_worker(std::string url);
    static bool download_progress(size_t got, size_t total, void* ctx);
};
