#include "wizard_activity.h"
#include "session.h"

static NVGcolor footer_hint_color() { return nvgRGB(0x71, 0x71, 0x7A); }

WizardActivity::WizardActivity(size_t num_steps) : values_(num_steps) {}

WizardActivity::~WizardActivity() {
    zeroize_secrets();
    delete renderer_;
}

void WizardActivity::zeroize_secrets() {
    for (auto& v : values_) zeroize_string(v);
}

brls::View* WizardActivity::createContentView() {
    auto* frame = new brls::AppletFrame();
    frame->setTitle(wizard_title());

    content_box_ = new brls::Box(brls::Axis::COLUMN);
    content_box_->setPadding(40.0f);

    renderer_ = new WizardRenderer(content_box_, get_steps());

    frame->setContentView(content_box_);

    // AppletFrame children after setContentView: [header(0), content(1), footer(2)].
    // Clear the debug-placeholder rectangles from the footer and add the exit hint.
    auto& af_ch = frame->getChildren();
    if (af_ch.size() >= 3) {
        auto* footer = static_cast<brls::Box*>(af_ch[2]);
        auto& fc = footer->getChildren();
        while (!fc.empty()) footer->removeView(fc.front());

        auto* exit_hint = new brls::Label();
        exit_hint->setText("  +  Exit");
        exit_hint->setFontSize(18.0f);
        exit_hint->setTextColor(footer_hint_color());
        footer->addView(exit_hint);
    }

    return frame;
}

void WizardActivity::onContentAvailable() {
    refresh();

    registerAction("Edit", brls::BUTTON_A, [this](brls::View*) {
        edit_current_field();
        return true;
    });
    registerAction("Next", brls::BUTTON_RB, [this](brls::View*) {
        go_next();
        return true;
    });
    registerAction("Back", brls::BUTTON_LB, [this](brls::View*) {
        go_back();
        return true;
    });
    registerAction("Exit", brls::BUTTON_B, [this](brls::View*) {
        zeroize_secrets();
        brls::Application::popActivity();
        return true;
    });

    register_extra_actions();
}

void WizardActivity::refresh() {
    std::string action = (current_step_ == values_.size() - 1) ? finish_label() : "Next";

    bool had_content = !content_box_->getChildren().empty();
    if (had_content)
        brls::Application::giveFocus(nullptr);

    renderer_->rebuild(current_step_, values_, error_, [this]{ edit_current_field(); });

    if (auto* cv = getContentView()) {
        cv->updateActionHint(brls::BUTTON_RB, action);
        if (had_content)
            brls::Application::giveFocus(cv);
    }
}

bool WizardActivity::validate_step(size_t /*step*/) { return true; }

void WizardActivity::register_extra_actions() {}

void WizardActivity::go_next() {
    size_t n = values_.size();
    if (current_step_ < n - 1) {
        if (!validate_step(current_step_)) {
            refresh();
            return;
        }
        current_step_++;
        error_.clear();
        refresh();
    } else {
        if (!validate_step(current_step_)) {
            refresh();
            return;
        }
        on_finish();
    }
}

void WizardActivity::go_back() {
    if (current_step_ > 0) {
        current_step_--;
        error_.clear();
        refresh();
    } else {
        zeroize_secrets();
        brls::Application::popActivity();
    }
}
