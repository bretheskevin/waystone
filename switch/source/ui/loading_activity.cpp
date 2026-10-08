#include "loading_activity.h"
#include "sync_controller.h"
#include "title_list_activity.h"
#include "keymap_switch.h"
#include <cstdio>
#include <cmath>

extern "C" {
struct Vault;
#include "waystone.h"
}

LoadingActivity::LoadingActivity(Session* session,
                                 std::function<LoadResult()> worker,
                                 std::function<void(const std::string&)> on_failure)
    : session_(session), worker_fn_(std::move(worker)), on_failure_(std::move(on_failure)) {}

LoadingActivity::~LoadingActivity() {
    // Safe to stop/delete pump here: destructor runs outside of run().
    if (pump_) { pump_->stop(); delete pump_; pump_ = nullptr; }
    if (worker_thread_.joinable()) worker_thread_.join();
}

brls::View* LoadingActivity::createContentView() {
    auto* frame = new brls::Box(brls::Axis::COLUMN);
    frame->setJustifyContent(brls::JustifyContent::CENTER);
    frame->setAlignItems(brls::AlignItems::CENTER);
    frame->setGrow(1.0f);

    // Spinner container: a fixed-size box with a rotating indicator
    spinner_box_ = new brls::Box(brls::Axis::ROW);
    spinner_box_->setWidth(80.0f);
    spinner_box_->setHeight(80.0f);
    spinner_box_->setAlignItems(brls::AlignItems::CENTER);
    spinner_box_->setJustifyContent(brls::JustifyContent::CENTER);
    spinner_box_->setMargins(0.0f, 0.0f, 20.0f, 0.0f);

    // Spinner visual: four dots arranged in a row, animated via opacity.
    // Note: brls::Rectangle::draw uses nvgRect (not nvgRoundedRect) in this
    // borealis fork, so setCornerRadius is a no-op visually (dots are square).
    // The animation (opacity cycling) still works correctly.
    for (int i = 0; i < 4; i++) {
        auto* dot = new brls::Rectangle();
        dot->setWidth(12.0f);
        dot->setHeight(12.0f);
        dot->setCornerRadius(6.0f);  // no-op in this fork's Rectangle::draw
        float alpha = 1.0f - (static_cast<float>(i) * 0.25f);
        dot->setColor(nvgRGBAf(0.34f, 0.34f, 0.85f, alpha));
        dot->setMargins(4.0f, 4.0f, 4.0f, 4.0f);
        spinner_box_->addView(dot);
    }
    frame->addView(spinner_box_);

    status_label_ = new brls::Label();
    status_label_->setText("Loading\xe2\x80\xa6");
    status_label_->setFontSize(22.0f);
    status_label_->setTextColor(nvgRGB(0x71, 0x71, 0x7A));
    frame->addView(status_label_);

    return frame;
}

void LoadingActivity::onContentAvailable() {
    printf("[ui] LoadingActivity content available\n");
    pump_ = new RefreshPump(this);
    pump_->start();

    done_.store(false);
    printf("[ui] LoadingActivity worker thread start\n");
    worker_thread_ = std::thread([this]() {
        result_ = worker_fn_();
        printf("[ui] LoadingActivity worker done: %s\n", result_.success ? "ok" : result_.error.c_str());
        done_.store(true);
    });
}

void LoadingActivity::RefreshPump::run() {
    owner_->update_spinner();
    if (!owner_->handled_ && owner_->done_.load()) {
        owner_->on_worker_done();
    }
}

void LoadingActivity::update_spinner() {
    spinner_angle_ += 0.15f;
    if (spinner_angle_ > 6.28f) spinner_angle_ -= 6.28f;

    if (spinner_box_) {
        size_t count = spinner_box_->getChildren().size();
        for (size_t i = 0; i < count; i++) {
            float phase = spinner_angle_ + (static_cast<float>(i) * 1.57f);
            float alpha = 0.3f + 0.7f * ((std::sin(phase) + 1.0f) / 2.0f);
            auto* rect = static_cast<brls::Rectangle*>(spinner_box_->getChildren()[i]);
            if (rect) rect->setColor(nvgRGBAf(0.34f, 0.34f, 0.85f, alpha));
        }
    }
}

void LoadingActivity::on_worker_done() {
    handled_ = true;  // prevent re-entry from the pump

    if (worker_thread_.joinable()) worker_thread_.join();

    // SAFETY: we are inside RefreshPump::run(). NEVER delete the pump,
    // popActivity, or do anything that destroys this activity from here.
    // pushActivity is safe (adds on top, does not destroy us).
    // Pump cleanup happens in ~LoadingActivity().

    if (result_.success) {
        printf("[ui] LoadingActivity success -> push_dashboard\n");
        push_dashboard_deferred();
    } else {
        if (on_failure_) {
            printf("[ui] LoadingActivity failure (on_failure cb) -> UnlockActivity\n");
            on_failure_(result_.error);
        } else {
            printf("[ui] LoadingActivity failure -> show_error_ui: %s\n", result_.error.c_str());
            show_error_ui(result_.error);
        }
    }
}

void LoadingActivity::show_error_ui(const std::string& error) {
    if (status_label_) {
        status_label_->setText(error);
        status_label_->setTextColor(nvgRGB(0xEF, 0x44, 0x44));
    }
    if (spinner_box_) spinner_box_->setVisibility(brls::Visibility::GONE);

    // B-button to go back (safe: action dispatch, not inside RepeatingTask)
    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [](brls::View*) {
        printf("[ui] loading error: back\n");
        brls::Application::popActivity();
        return true;
    });
}

void LoadingActivity::push_dashboard_deferred() {
    // BUG 2 fix: make LoadingActivity translucent before pushActivity so borealis
    // uses fadeOut=false. With fadeOut=true, setAlpha(0.0f) is called on the incoming
    // activity; View::show() early-returns (hidden=false on a fresh view), so alpha
    // is never animated back → permanent black screen.
    // With fadeOut=false (last->isTranslucent()), setAlpha(0) is skipped entirely
    // and the dashboard stays at alpha=1.0f (Animatable default).
    if (auto* cv = getContentView()) cv->setInFadeAnimation(true);
    auto* ctrl = new SyncController(session_->vault, session_->uid, session_->device_id,
                                    session_->dav.as_cfg(), std::move(result_.titles), &session_->config);
    brls::Application::pushActivity(new TitleListActivity(ctrl, session_));
}
