#include "overlay.h"
#include "toast.h"
#include "ws_views.h"
#include <cstdio>

namespace {

// One-shot: runs fn on the next Ticking update, then frees itself.
class DeferredCall : public brls::Ticking {
  public:
    explicit DeferredCall(std::function<void()> fn) : fn_(std::move(fn)) {}

  protected:
    bool onUpdate(brls::Time) override { return false; }
    // Last thing Ticking::stop() touches, so self-deletion here is safe.
    void onStop() override {
        auto fn = std::move(fn_);
        delete this;
        if (fn) fn();
    }

  private:
    std::function<void()> fn_;
};

class OverlayRoot : public brls::Box {
  public:
    explicit OverlayRoot(bool dim) : dim_(dim) {}

    bool isTranslucent() override { return true; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override {
        if (dim_) {
            nvgSave(vg);
            nvgGlobalAlpha(vg, getAlpha());
            ws_nvg_fill_rect(vg, 0.0f, 0.0f, WS_SCREEN_W, WS_SCREEN_H, 0.0f, WS_C_DIM);
            nvgRestore(vg);
        }
        brls::Box::draw(vg, x, y, width, height, style, ctx);
        toast_host_.draw(vg);
    }

  private:
    bool        dim_;
    WsToastHost toast_host_;
};

}

OverlayActivity::OverlayActivity(bool dim) : dim_(dim) {}

OverlayActivity::~OverlayActivity() {
    printf("[ui] %s %p: popped\n", kind_name_, static_cast<void*>(this));
    if (after_) (new DeferredCall(std::move(after_)))->start();
}

void OverlayActivity::push(OverlayActivity* overlay) {
    overlay->kind_name_ = overlay->kind();
    printf("[ui] %s %p: push\n", overlay->kind_name_, static_cast<void*>(overlay));
    brls::Application::pushActivity(overlay, brls::TransitionAnimation::FADE);
    overlay->playEnter();
}

void OverlayActivity::close(std::function<void()> after) {
    if (closing_) {
        printf("[ui] %s %p: close ignored (already closing)\n", kind(), static_cast<void*>(this));
        return;
    }
    closing_ = true;
    after_   = std::move(after);
    printf("[ui] %s %p: close\n", kind(), static_cast<void*>(this));
    brls::Application::blockInputs();
    playExit([this] {
        brls::Application::unblockInputs();
        printf("[ui] %s %p: exit animation done, popping\n", kind(), static_cast<void*>(this));
        brls::Application::popActivity(brls::TransitionAnimation::FADE, [] {});
    });
}

brls::View* OverlayActivity::createContentView() {
    root_ = new OverlayRoot(dim_);
    buildBody(makeContainer(root_));
    return root_;
}

// borealis has no TransitionAnimation::NONE: overlays push/pop with FADE and shrink its
// fade to ~1 ms, since they animate themselves.
float OverlayActivity::getShowAnimationDuration(brls::TransitionAnimation) { return 1.0f; }

SlidePanel::SlidePanel(float width, bool dim) : OverlayActivity(dim), width_(width) {}

void SlidePanel::setGuide(const std::string& text) {
    guide_text_ = text;
    if (guide_) guide_->setText(text);
}

brls::Box* SlidePanel::makeContainer(brls::Box* root) {
    panel_ = new brls::Box(brls::Axis::COLUMN);
    panel_->setPositionType(brls::PositionType::ABSOLUTE);
    panel_->setPositionTop(0.0f);
    panel_->setPositionRight(0.0f);
    panel_->setDimensions(width_, WS_SCREEN_H);
    panel_->setBackgroundColor(ws_nvg(WS_C_PANEL));
    root->addView(panel_);

    guide_ = new GuidePill();
    guide_->setText(guide_text_);
    root->addView(guide_);

    root->registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        close();
        return true;
    });
    return panel_;
}

void SlidePanel::padBody(brls::Box* body) {
    body->setPadding(WS_DIALOG_PAD, 0.0f, WS_SCREEN_H - WS_RULE_BOTTOM_Y, 0.0f);
}

brls::Label* SlidePanel::addInsetText(brls::Box* body, brls::Label* label, float margin_top) const {
    label->setWidth(width_ - 2.0f * WS_ROW_INSET);
    label->setMarginLeft(WS_ROW_INSET);
    label->setMarginTop(margin_top);
    body->addView(label);
    return label;
}

void SlidePanel::slide(float from, float to, std::function<void()> done) {
    slide_.reset(from);
    slide_.addStep(to, WS_ANIM_SLIDE_MS, brls::EasingFunction::quadraticOut);
    auto apply = [this] {
        float t = slide_.getValue();
        panel_->setTranslationX(t * width_);
        guide_->setTranslationX(t * guide_->getWidth());
    };
    slide_.setTickCallback(apply);
    slide_.setEndCallback([done](bool) { if (done) done(); });
    apply();
    slide_.start();
}

void SlidePanel::playEnter() { slide(1.0f, 0.0f, {}); }

void SlidePanel::playExit(std::function<void()> done) { slide(slide_.getValue(), 1.0f, std::move(done)); }

DialogCard::DialogCard(float width) : OverlayActivity(true), width_(width) {}

void DialogCard::setHint(const std::string& text) {
    hint_text_ = text;
    if (hint_) hint_->setText(text);
}

brls::Box* DialogCard::makeContainer(brls::Box* root) {
    root->setJustifyContent(brls::JustifyContent::CENTER);
    root->setAlignItems(brls::AlignItems::CENTER);

    auto* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(width_);
    card->setPadding(WS_DIALOG_PAD);
    card->setBackgroundColor(ws_nvg(WS_C_DIALOG));
    card->setCornerRadius(WS_DIALOG_RADIUS);
    root->addView(card);

    auto* body = new brls::Box(brls::Axis::COLUMN);
    body->setWidth(innerWidth());
    card->addView(body);

    hint_ = ws_make_text(hint_text_, WS_DIALOG_HINT_SIZE, WS_C_TEXT);
    hint_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    hint_->setWidth(innerWidth());
    hint_->setMarginTop(WS_DIALOG_PAD);
    card->addView(hint_);
    return body;
}

brls::Label* DialogCard::addTitle(brls::Box* body, const std::string& text) {
    brls::Label* label = ws_make_text(text, WS_DIALOG_TITLE_SIZE, WS_C_TEXT);
    label->setWidth(innerWidth());
    body->addView(label);
    return label;
}

brls::Label* DialogCard::addDetail(brls::Box* body, const std::string& text) {
    brls::Label* label = ws_make_text(text, WS_DIALOG_DETAIL_SIZE, WS_C_DETAIL);
    label->setWidth(innerWidth());
    label->setMarginTop(WS_DIALOG_PAD / 2.0f);
    body->addView(label);
    return label;
}
