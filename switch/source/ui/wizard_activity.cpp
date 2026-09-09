#include "wizard_activity.h"
#include "session.h"
#include "swkbd_util.h"

static NVGcolor footer_hint_color() { return nvgRGB(0x71, 0x71, 0x7A); }

// NintendoExt (PlSharedFontType_NintendoExt) private-use button glyph codepoints:
//   U+E0A0  "\xEE\x82\xA0"  A button
//   U+E0A5  "\xEE\x82\xA5"  R shoulder button (Switch nomenclature: R, not RB)
//   U+E0A4  "\xEE\x82\xA4"  L shoulder button (Switch nomenclature: L, not LB)
//   U+E0B5  "\xEE\x82\xB5"  Plus (+) button  [source: WerWolv/libtesla, confirmed NintendoExt]
// On Switch hardware these render via the NintendoExt system font registered as
// FONT_SWITCH_ICONS (fallback of FONT_REGULAR).  On desktop/preview the font is
// absent (User-Switch-Icons.ttf not provided), so they appear as replacement boxes.
static std::string build_hint_text(const std::string& rb_label) {
    return "\xEE\x82\xA0 Edit   \xc2\xb7   \xEE\x82\xA5 " + rb_label
         + "   \xc2\xb7   \xEE\x82\xA4 Back   \xc2\xb7   \xEE\x82\xB5 Exit";
}

WizardActivity::WizardActivity(size_t num_values) : values_(num_values) {}

WizardActivity::~WizardActivity() {
    if (refresh_pump_) {
        refresh_pump_->stop();
        delete refresh_pump_;
        refresh_pump_ = nullptr;
    }
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
    auto& af_ch = frame->getChildren();
    if (af_ch.size() >= 3) {
        auto* footer = static_cast<brls::Box*>(af_ch[2]);
        auto& fc = footer->getChildren();
        while (!fc.empty()) footer->removeView(fc.front());

        hint_label_ = new brls::Label();
        hint_label_->setText(build_hint_text("Next"));
        hint_label_->setFontSize(18.0f);
        hint_label_->setTextColor(footer_hint_color());
        footer->addView(hint_label_);
    }

    return frame;
}

void WizardActivity::onContentAvailable() {
    refresh();  // synchronous: not inside an action dispatch, safe for frame 1

    refresh_pump_ = new RefreshPump(this);
    refresh_pump_->start();

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

void WizardActivity::schedule_refresh() {
    refresh_pending_ = true;
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
    schedule_refresh();
}

void WizardActivity::refresh() {
    bool is_last = (current_step_ == steps_.size() - 1);
    std::string action = is_last ? finish_label() : "Next";

    WizardTransition trans = WizardTransition::NONE;
    if (step_changed_) {
        trans = go_forward_ ? WizardTransition::FORWARD : WizardTransition::BACK;
        step_changed_ = false;
    }

    std::string flabel = is_last ? action : std::string{};
    auto        fcb    = is_last ? std::function<void()>([this]{ go_next(); })
                                 : std::function<void()>{};

    renderer_->rebuild(current_step_, values_, error_,
                       [this](const WizardFieldDef& f) { edit_field(f); },
                       flabel, fcb, trans);

    if (hint_label_)
        hint_label_->setText(build_hint_text(action));

    if (auto* cv = getContentView()) {
        cv->updateActionHint(brls::BUTTON_RB, action);
        brls::Application::giveFocus(cv);
    }
}

bool WizardActivity::validate_step(size_t /*step*/) { return true; }

void WizardActivity::register_extra_actions() {}

void WizardActivity::go_next() {
    size_t n = steps_.size();
    if (current_step_ < n - 1) {
        if (!validate_step(current_step_)) {
            schedule_refresh();
            return;
        }
        current_step_++;
        error_.clear();
        step_changed_ = true;
        go_forward_   = true;
        schedule_refresh();
    } else {
        if (!validate_step(current_step_)) {
            schedule_refresh();
            return;
        }
        on_finish();
    }
}

void WizardActivity::go_back() {
    if (current_step_ > 0) {
        current_step_--;
        error_.clear();
        step_changed_ = true;
        go_forward_   = false;
        schedule_refresh();
    } else {
        zeroize_secrets();
        brls::Application::popActivity();
    }
}
