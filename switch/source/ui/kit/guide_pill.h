#pragma once
#include <borealis.hpp>
#include <string>

// Bottom-right controller-hint pill. Detached: always sits at the guide geometry
// (right-anchored at WS_GUIDE_Y) whatever its parent's layout is. Non-focusable.
class GuidePill : public brls::View {
  public:
    GuidePill();

    void setText(const std::string& text);

    // Slides in from x=1280 (WS_ANIM_SLIDE_MS, ease-out). Owners call it from onContentAvailable.
    void slideIn();

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    void place();

    std::string      text_;
    float            pill_w_ = 0.0f;
    brls::Animatable slide_{0.0f};
};
