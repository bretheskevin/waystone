#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include "guide_pill.h"
#include "ws_style.h"
#include "../keymap_switch.h"

// Translucent Activity drawn over the one below (which keeps rendering and gets its focus
// back after the pop). Push with OverlayActivity::push(); close with close(after).
// `after` runs one frame after the overlay has been popped AND deleted, so it may push
// another overlay; it must not capture the closed overlay's `this`.
class OverlayActivity : public brls::Activity {
  public:
    explicit OverlayActivity(bool dim);
    ~OverlayActivity() override;

    static void push(OverlayActivity* overlay);

    // Plays the exit animation, then pops. Idempotent while closing.
    void close(std::function<void()> after = {});
    bool isClosing() const { return closing_; }

    brls::View* createContentView() override;
    float getShowAnimationDuration(brls::TransitionAnimation animation) override;

  protected:
    // Fill the overlay. `body` is the panel / card container (the full-screen root for a
    // bare OverlayActivity). Register actions on root(), never on `body`.
    virtual void buildBody(brls::Box* body) = 0;

    virtual brls::Box* makeContainer(brls::Box* root) { return root; }
    virtual void playEnter() {}
    virtual void playExit(std::function<void()> done) { done(); }
    virtual const char* kind() const { return "overlay"; }

    brls::Box* root() const { return root_; }

  private:
    bool                  dim_;
    bool                  closing_   = false;
    const char*           kind_name_ = "overlay";
    brls::Box*            root_    = nullptr;
    std::function<void()> after_;
};

// Right-anchored full-height panel that slides in/out; B (WsAction::Back) closes it.
// Stack a second panel with dim=false.
class SlidePanel : public OverlayActivity {
  public:
    SlidePanel(float width, bool dim);

    void setGuide(const std::string& text);

  protected:
    brls::Box* makeContainer(brls::Box* root) override;
    void playEnter() override;
    void playExit(std::function<void()> done) override;
    const char* kind() const override { return "panel"; }

    // Body padding: top inset, bottom clear of the guide pill band. Call first in buildBody.
    static void padBody(brls::Box* body);
    // Appends `label` to `body`, sized and inset like MenuList row text.
    brls::Label* addInsetText(brls::Box* body, brls::Label* label, float margin_top) const;

  private:
    void slide(float from, float to, std::function<void()> done);

    float            width_;
    brls::Box*       panel_ = nullptr;
    GuidePill*       guide_ = nullptr;
    std::string      guide_text_;
    brls::Animatable slide_{0.0f};
};

// Centered rounded card with a bottom hint line. Always dims.
class DialogCard : public OverlayActivity {
  public:
    explicit DialogCard(float width = WS_DIALOG_W);

    void setHint(const std::string& text);

  protected:
    brls::Box* makeContainer(brls::Box* root) override;
    const char* kind() const override { return "dialog"; }

    brls::Label* addTitle(brls::Box* body, const std::string& text);
    brls::Label* addDetail(brls::Box* body, const std::string& text);
    float innerWidth() const { return width_ - 2.0f * WS_DIALOG_PAD; }

  private:
    float        width_;
    brls::Label* hint_ = nullptr;
    std::string  hint_text_;
};
