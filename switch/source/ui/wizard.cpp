#include "wizard.h"

static NVGcolor dot_filled()  { return nvgRGB(0x63, 0x66, 0xF1); }
static NVGcolor dot_empty()   { return nvgRGB(0xA1, 0xA1, 0xAA); }
static NVGcolor error_color() { return nvgRGB(0xEF, 0x44, 0x44); }
static NVGcolor hint_color()  { return nvgRGB(0x71, 0x71, 0x7A); }

WizardRenderer::WizardRenderer(brls::Box* parent, const std::vector<WizardStepDef>& steps)
    : parent_(parent), steps_(steps) {}

void WizardRenderer::clear_parent() {
    auto& ch = parent_->getChildren();
    while (!ch.empty())
        parent_->removeView(ch.front());
}

brls::Box* WizardRenderer::build_progress_dots(size_t current, size_t total) {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setMargins(8.0f, 0.0f, 16.0f, 0.0f);
    for (size_t i = 0; i < total; i++) {
        auto* dot = new brls::Rectangle(i <= current ? dot_filled() : dot_empty());
        dot->setWidth(12.0f);
        dot->setHeight(12.0f);
        dot->setCornerRadius(6.0f);
        if (i > 0) dot->setMarginLeft(8.0f);
        row->addView(dot);
    }
    return row;
}

void WizardRenderer::rebuild(size_t current_step,
                             const std::vector<std::string>& values,
                             const std::string& error) {
    clear_parent();

    parent_->addView(build_progress_dots(current_step, steps_.size()));

    const auto& step = steps_[current_step];

    auto* title = new brls::Label();
    title->setText(step.label);
    title->setFontSize(32.0f);
    title->setSingleLine(true);
    title->setMargins(16.0f, 0.0f, 8.0f, 0.0f);
    parent_->addView(title);

    if (current_step < values.size()) {
        const std::string& raw = values[current_step];
        if (!raw.empty() || !step.placeholder.empty()) {
            auto* val_label = new brls::Label();
            if (raw.empty()) {
                val_label->setText(step.placeholder);
                val_label->setTextColor(hint_color());
            } else if (step.is_secret) {
                val_label->setText(std::string(raw.size(), '*'));
            } else {
                val_label->setText(raw);
            }
            val_label->setFontSize(24.0f);
            val_label->setSingleLine(true);
            val_label->setMargins(4.0f, 0.0f, 4.0f, 0.0f);
            parent_->addView(val_label);
        }
    }

    if (!step.hint.empty()) {
        auto* hint = new brls::Label();
        hint->setText(step.hint);
        hint->setFontSize(18.0f);
        hint->setTextColor(hint_color());
        hint->setMargins(4.0f, 0.0f, 8.0f, 0.0f);
        parent_->addView(hint);
    }

    if (!error.empty()) {
        auto* err = new brls::Label();
        err->setText(error);
        err->setFontSize(18.0f);
        err->setTextColor(error_color());
        err->setMargins(4.0f, 0.0f, 8.0f, 0.0f);
        parent_->addView(err);
    }
}
