#include "wizard_activity.h"
#include "applet_footer_hint.h"
#include "keymap_switch.h"
#include "session.h"
#include "swkbd_util.h"
#include <cstdio>

static std::string build_hint_text(const std::string& next_label, bool show_back) {
    const std::string sep = ws_hint_style().item_sep;
    std::string s = ws_hint(WsAction::WizardEdit) + sep
                  + ws_hint(WsAction::WizardNext, next_label.c_str());
    if (show_back) s += sep + ws_hint(WsAction::WizardPrev);
    s += sep + ws_hint(WsAction::Quit);
    return s;
}

WizardActivity::WizardActivity(size_t num_values) : values_(num_values) {}

WizardActivity::~WizardActivity() {
    pump_.stop();  // stop BEFORE zeroize_secrets / delete renderer_
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

    steps_    = get_steps();
    renderer_ = new WizardRenderer(content_box_, steps_);

    frame->setContentView(content_box_);

    // AppletFrame children after setContentView: [header(0), content(1), footer(2)].
    // Clear the debug-placeholder rectangles from the footer and add the full hint bar.
    hint_label_ = set_footer_hint(frame, build_hint_text(ws_label(WsAction::WizardNext), false));

    return frame;
}

void WizardActivity::onContentAvailable() {
    printf("[ui] wizard content available\n");
    refresh();  // synchronous: not inside an action dispatch, safe for frame 1

    pump_.start();

    registerAction(ws_label(WsAction::WizardNext), ws_brls(WsAction::WizardNext), [this](brls::View*) {
        go_next();
        return true;
    });
    registerAction(ws_label(WsAction::WizardPrev), ws_brls(WsAction::WizardPrev), [this](brls::View*) {
        go_back();
        return true;
    });
    registerAction(ws_label(WsAction::WizardPrevAlt), ws_brls(WsAction::WizardPrevAlt), [this](brls::View*) {
        go_back();
        return true;
    });

    register_extra_actions();
}

void WizardActivity::schedule_refresh() {
    pump_.schedule();
}

void WizardActivity::reload_steps() {
    steps_    = get_steps();
    delete renderer_;
    renderer_ = new WizardRenderer(content_box_, steps_);
    step_changed_ = false;
}

void WizardActivity::edit_field(const WizardFieldDef& f) {
    std::string result = swkbd_prompt(f.label.c_str(), values_[f.value_index], f.is_secret);
    if (!result.empty() || !f.is_secret) {
        if (f.is_secret) {
            zeroize_string(values_[f.value_index]);
            values_[f.value_index] = result;
            zeroize_string(result);
        } else {
            values_[f.value_index] = std::move(result);
        }
    } else {
        zeroize_string(result);
    }
    error_.clear();
    status_.clear();
    schedule_refresh();
}

void WizardActivity::refresh() {
    bool is_last = (current_step_ == steps_.size() - 1);
    std::string action = is_last ? finish_label() : std::string(ws_label(WsAction::WizardNext));

    WizardTransition trans = WizardTransition::NONE;
    if (step_changed_) {
        trans = go_forward_ ? WizardTransition::FORWARD : WizardTransition::BACK;
        step_changed_ = false;
    }

    std::string flabel = is_last ? action : std::string{};
    auto        fcb    = is_last ? std::function<void()>([this]{ go_next(); })
                                 : std::function<void()>{};

    renderer_->rebuild(current_step_, values_, error_, status_,
                       [this](const WizardFieldDef& f) { edit_field(f); },
                       flabel, fcb, trans);

    if (hint_label_)
        hint_label_->setText(build_hint_text(action, current_step_ > 0));

    if (auto* cv = getContentView()) {
        cv->updateActionHint(ws_brls(WsAction::WizardNext), action);
        brls::Application::giveFocus(cv);
    }
}

bool WizardActivity::validate_step(size_t /*step*/) { return true; }

void WizardActivity::register_extra_actions() {}

void WizardActivity::go_next() {
    size_t n = steps_.size();
    if (!validate_step(current_step_)) {
        printf("[ui] wizard step %zu: validation failed\n", current_step_);
        schedule_refresh();
        return;
    }
    if (current_step_ < n - 1) {
        printf("[ui] wizard step %zu -> %zu (next)\n", current_step_, current_step_ + 1);
        current_step_++;
        error_.clear();
        status_.clear();
        step_changed_ = true;
        go_forward_   = true;
        schedule_refresh();
    } else {
        printf("[ui] wizard finish on step %zu\n", current_step_);
        on_finish();
    }
}

void WizardActivity::go_back() {
    if (current_step_ == 0) {
        printf("[ui] wizard back ignored on first step\n");
        return;
    }
    printf("[ui] wizard step %zu -> %zu (back)\n", current_step_, current_step_ - 1);
    current_step_--;
    error_.clear();
    status_.clear();
    step_changed_ = true;
    go_forward_   = false;
    schedule_refresh();
}
