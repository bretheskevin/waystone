#include "wizard.h"

static NVGcolor dot_filled()  { return nvgRGB(0x63, 0x66, 0xF1); }
static NVGcolor dot_empty()   { return nvgRGB(0xA1, 0xA1, 0xAA); }
static NVGcolor error_color() { return nvgRGB(0xEF, 0x44, 0x44); }
static NVGcolor hint_color()  { return nvgRGB(0x71, 0x71, 0x7A); }

static constexpr float   SLIDE_DIST = 64.0f;
#ifdef PREVIEW_SLOW_TRANSITION
static constexpr int32_t SLIDE_MS = 2000;
#else
static constexpr int32_t SLIDE_MS = 260;
#endif

WizardRenderer::WizardRenderer(brls::Box* parent, const std::vector<WizardStepDef>& steps)
    : parent_(parent), steps_(steps) {}

WizardRenderer::~WizardRenderer() {
    // Stop animations without firing the delete-view endCallback — the view
    // tree is cleaned up by AppletFrame when the Activity is destroyed.
    out_tx_.stop();
    in_tx_.stop();
    outgoing_box_ = nullptr;
    in_transition_ = false;
}

// ---------------------------------------------------------------------------
// Dots row
// ---------------------------------------------------------------------------

brls::Box* WizardRenderer::build_dots_row(size_t current, size_t total) {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setMargins(8.0f, 0.0f, 16.0f, 0.0f);
    for (size_t i = 0; i < total; i++) {
        float sz = (i == current) ? 14.0f : 12.0f;
        auto* dot = new brls::Rectangle(i <= current ? dot_filled() : dot_empty());
        dot->setWidth(sz);
        dot->setHeight(sz);
        dot->setCornerRadius(sz / 2.0f);
        if (i > 0) dot->setMarginLeft(8.0f);
        row->addView(dot);
    }
    return row;
}

// Remove old dots_row_ (deletes it) and add a fresh one at the front of parent_.
void WizardRenderer::refresh_dots(size_t current) {
    if (!dots_row_) return;
    parent_->removeView(dots_row_);          // deletes old row
    dots_row_ = build_dots_row(current, steps_.size());
    parent_->addView(dots_row_, 0);          // insert at front (step_slot_ shifts to 1)
}

// ---------------------------------------------------------------------------
// Step content builder
// ---------------------------------------------------------------------------

// Returns an ABSOLUTE-positioned Box that fills step_slot_ 100%x100%.
// Welcome step (0 fields): content is centered vertically + horizontally.
// Other steps: top-aligned.
brls::Box* WizardRenderer::make_step_content(
    size_t step,
    const std::vector<std::string>& values,
    const std::string& error,
    const std::function<void(const WizardFieldDef&)>& edit_cb,
    const std::string& finish_label,
    const std::function<void()>& finish_cb)
{
    const auto& s = steps_[step];
    bool welcome  = s.fields.empty();

    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setPositionType(brls::PositionType::ABSOLUTE);
    box->setWidthPercentage(100);
    box->setHeightPercentage(100);

    if (welcome) {
        box->setJustifyContent(brls::JustifyContent::CENTER);
        box->setAlignItems(brls::AlignItems::CENTER);

        auto* title = new brls::Label();
        title->setText(s.title);
        title->setFontSize(32.0f);
        title->setSingleLine(false);
        title->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
        box->addView(title);

        if (!s.hint.empty()) {
            auto* hint = new brls::Label();
            hint->setText(s.hint);
            hint->setFontSize(18.0f);
            hint->setTextColor(hint_color());
            box->addView(hint);
        }
        return box;
    }

    // Normal step: top-aligned
    bool multi = s.fields.size() > 1;

    auto* title = new brls::Label();
    title->setText(s.title);
    title->setFontSize(32.0f);
    title->setSingleLine(true);
    title->setMargins(16.0f, 0.0f, 12.0f, 0.0f);
    box->addView(title);

    for (const auto& f : s.fields) {
        if (multi) {
            auto* lbl = new brls::Label();
            lbl->setText(f.label);
            lbl->setFontSize(20.0f);
            lbl->setMargins(8.0f, 0.0f, 4.0f, 0.0f);
            box->addView(lbl);
        }

        const std::string& raw = values[f.value_index];
        auto* btn = new brls::Button();
        btn->setStyle(&brls::BUTTONSTYLE_BORDERED);
        if (raw.empty())
            btn->setText(f.placeholder);
        else if (f.is_secret)
            btn->setText(std::string(raw.size(), '*'));
        else
            btn->setText(raw);
        btn->setMargins(0.0f, 0.0f, 4.0f, 0.0f);
        if (edit_cb)
            btn->registerClickAction([edit_cb, f](brls::View*) { edit_cb(f); return true; });
        box->addView(btn);

        if (!f.hint.empty()) {
            auto* fhint = new brls::Label();
            fhint->setText(f.hint);
            fhint->setFontSize(18.0f);
            fhint->setTextColor(hint_color());
            fhint->setMargins(2.0f, 0.0f, 8.0f, 0.0f);
            box->addView(fhint);
        }
    }

    if (!error.empty()) {
        auto* err = new brls::Label();
        err->setText(error);
        err->setFontSize(18.0f);
        err->setTextColor(error_color());
        err->setMargins(4.0f, 0.0f, 8.0f, 0.0f);
        box->addView(err);
    }

    if (!finish_label.empty() && finish_cb) {
        auto* fbtn = new brls::Button();
        fbtn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        fbtn->setText(finish_label);
        fbtn->setMargins(16.0f, 0.0f, 0.0f, 0.0f);
        fbtn->registerClickAction([finish_cb](brls::View*) { finish_cb(); return true; });
        box->addView(fbtn);
    }

    return box;
}

// ---------------------------------------------------------------------------
// Layout helpers
// ---------------------------------------------------------------------------

void WizardRenderer::init_layout(
    size_t step,
    const std::vector<std::string>& values,
    const std::string& error,
    const std::function<void(const WizardFieldDef&)>& edit_cb,
    const std::string& finish_label,
    const std::function<void()>& finish_cb)
{
    // Clear orphaned views left by a previous renderer (e.g. after reload_steps()).
    brls::Application::giveFocus(nullptr);
    auto& ch = parent_->getChildren();
    while (!ch.empty()) parent_->removeView(ch.front());

    dots_row_ = build_dots_row(step, steps_.size());
    parent_->addView(dots_row_);

    step_slot_ = new brls::Box(brls::Axis::COLUMN);
    step_slot_->setGrow(1.0f);
    parent_->addView(step_slot_);

    active_box_ = make_step_content(step, values, error, edit_cb, finish_label, finish_cb);
    step_slot_->addView(active_box_);
}

// Same-step refresh (error re-render): instant swap of step content, no animation.
void WizardRenderer::swap_instant(
    size_t step,
    const std::vector<std::string>& values,
    const std::string& error,
    const std::function<void(const WizardFieldDef&)>& edit_cb,
    const std::string& finish_label,
    const std::function<void()>& finish_cb)
{
    brls::Application::giveFocus(nullptr);
    step_slot_->removeView(active_box_);     // deletes active_box_
    active_box_ = make_step_content(step, values, error, edit_cb, finish_label, finish_cb);
    step_slot_->addView(active_box_);
}

// If a transition is running, complete it immediately (move outgoing view to deletion).
void WizardRenderer::abort_transition() {
    if (!in_transition_) return;
    out_tx_.stop();   // fires endCallback(false) → deletes outgoing_box_, resets in_transition_
    in_tx_.stop();
    if (outgoing_box_) {
        step_slot_->removeView(outgoing_box_);
        outgoing_box_ = nullptr;
    }
    if (active_box_) {
        active_box_->setTranslationX(0.0f);
        active_box_->setAlpha(1.0f);
    }
    in_transition_ = false;
}

// ---------------------------------------------------------------------------
// Main rebuild entry point
// ---------------------------------------------------------------------------

void WizardRenderer::rebuild(
    size_t current_step,
    const std::vector<std::string>& values,
    const std::string& error,
    const std::function<void(const WizardFieldDef&)>& edit_cb,
    const std::string& finish_label,
    const std::function<void()>& finish_cb,
    WizardTransition transition)
{
    if (!step_slot_) {
        init_layout(current_step, values, error, edit_cb, finish_label, finish_cb);
        return;
    }

    if (in_transition_)
        abort_transition();

    refresh_dots(current_step);

    if (transition == WizardTransition::NONE) {
        swap_instant(current_step, values, error, edit_cb, finish_label, finish_cb);
        return;
    }

    // Animated slide+fade transition.
    // dir > 0: forward (old slides left, new comes from right).
    // dir < 0: back    (old slides right, new comes from left).
    float dir = (transition == WizardTransition::FORWARD) ? 1.0f : -1.0f;

    brls::Application::giveFocus(nullptr);

    // Build incoming content; start it invisible, offset to the side.
    brls::Box* new_box = make_step_content(current_step, values, error, edit_cb, finish_label, finish_cb);
    new_box->setAlpha(0.0f);
    new_box->setTranslationX(dir * SLIDE_DIST);

    // Insert new_box at position 0 so focus traversal finds it first.
    step_slot_->addView(new_box, 0);

    outgoing_box_ = active_box_;
    active_box_   = new_box;
    in_transition_ = true;

    // Out: old box slides away and fades out.
    out_tx_.reset(0.0f);
    out_tx_.addStep(-dir * SLIDE_DIST, SLIDE_MS, brls::EasingFunction::quadraticOut);
    out_tx_.setTickCallback([this] {
        if (!outgoing_box_) return;
        outgoing_box_->setTranslationX(out_tx_.getValue());
        outgoing_box_->setAlpha(1.0f - out_tx_.getProgress());
    });
    out_tx_.setEndCallback([this](bool) {
        if (outgoing_box_) {
            step_slot_->removeView(outgoing_box_);
            outgoing_box_ = nullptr;
        }
        in_transition_ = false;
    });

    // In: new box slides into position and fades in.
    in_tx_.reset(dir * SLIDE_DIST);
    in_tx_.addStep(0.0f, SLIDE_MS, brls::EasingFunction::quadraticOut);
    in_tx_.setTickCallback([this] {
        if (!active_box_) return;
        active_box_->setTranslationX(in_tx_.getValue());
        active_box_->setAlpha(in_tx_.getProgress());
    });

    out_tx_.start();
    in_tx_.start();
}
