#pragma once
#include <atomic>
#include <borealis.hpp>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include "session.h"
#include "saves.h"  // TitleInfo

// A loading screen with an animated spinner that runs heavy work on a
// background thread and pushes the dashboard when done.
//
// Usage (manual unlock):
//   new LoadingActivity(session, [session, pass_copy, keys_data, keys_len, recovery_mode]() -> LoadResult { ... })
//
// Usage (auto-unlock):
//   new LoadingActivity(session, [session, mdk_buf, webdav_pass]() -> LoadResult { ... })
//
// The worker_fn runs OFF the render thread. The LoadingActivity polls
// completion via RefreshPump (16ms) and pushes the dashboard from the
// main thread's poll callback.
class LoadingActivity : public brls::Activity {
public:
    struct LoadResult {
        bool success = false;
        std::string error;
        std::vector<TitleInfo> titles;  // populated by worker on success
    };

    // worker: runs on a background thread; must not touch borealis.
    // on_failure: optional callback when the worker fails (e.g., fall through to UnlockActivity).
    //             Called on the render thread from the poll callback.
    LoadingActivity(Session* session,
                    std::function<LoadResult()> worker,
                    std::function<void(const std::string& error)> on_failure = nullptr);
    ~LoadingActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    Session* session_;
    std::function<LoadResult()> worker_fn_;
    std::function<void(const std::string&)> on_failure_;

    // UI elements
    brls::Box*   spinner_box_   = nullptr;
    brls::Label* status_label_  = nullptr;

    // Worker thread + poll machinery
    std::thread          worker_thread_;
    std::atomic<bool>    done_{false};
    bool                 handled_ = false;  // main-thread-only: true once on_worker_done ran
    LoadResult           result_;

    // RefreshPump: polls done_ flag and spinner animation on the main thread.
    // SAFETY: run() must NEVER delete the pump or popActivity (UAF).
    // pushActivity is safe (adds on top, does not destroy this activity).
    class RefreshPump : public brls::RepeatingTask {
    public:
        explicit RefreshPump(LoadingActivity* owner)
            : brls::RepeatingTask(16), owner_(owner) {}
        void run() override;
    private:
        LoadingActivity* owner_;
    };
    RefreshPump* pump_ = nullptr;

    // Spinner animation state
    float spinner_angle_ = 0.0f;
    void update_spinner();
    void on_worker_done();
    void push_dashboard_deferred();
    void show_error_ui(const std::string& error);
};
