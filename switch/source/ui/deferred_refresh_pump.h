#pragma once
#include <borealis.hpp>
#include <functional>

// Deferred one-frame rebuild pump for borealis Activities. Rebuilding the
// focused view synchronously inside its own action is a use-after-free; callers
// schedule() from an action and the rebuild runs on the next frame, after input
// dispatch unwinds. Optional on_tick runs every frame (e.g. a worker poll).
class DeferredRefreshPump {
  public:
    explicit DeferredRefreshPump(std::function<void()> on_refresh,
                                 std::function<void()> on_tick = {})
        : on_refresh_(std::move(on_refresh)), on_tick_(std::move(on_tick)) {}
    ~DeferredRefreshPump() { stop(); }
    DeferredRefreshPump(const DeferredRefreshPump&)            = delete;
    DeferredRefreshPump& operator=(const DeferredRefreshPump&) = delete;

    void start()    { if (!task_) { task_ = new Task(this); task_->start(); } }
    void stop()     { if (task_) { task_->stop(); delete task_; task_ = nullptr; } }
    void schedule() { pending_ = true; }

  private:
    class Task : public brls::RepeatingTask {
      public:
        explicit Task(DeferredRefreshPump* o) : brls::RepeatingTask(16), o_(o) {}
        void run() override {
            if (o_->pending_) { o_->pending_ = false; if (o_->on_refresh_) o_->on_refresh_(); }
            // Copy before calling so the lambda can safely clear on_tick_
            // without destroying the currently-running instance.
            if (o_->on_tick_) { auto fn = o_->on_tick_; fn(); }
        }
      private:
        DeferredRefreshPump* o_;
    };

    std::function<void()> on_refresh_;
    std::function<void()> on_tick_;
    bool  pending_ = false;
    Task* task_    = nullptr;
};
