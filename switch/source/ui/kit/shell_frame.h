#pragma once
#include <borealis.hpp>
#include <string>
#include "guide_pill.h"
#include "toast.h"

// Root content view of a kit Activity: header (mark, title, status), rules, version label,
// content slot (y 89..647) and the guide pill. Registers no actions.
class ShellFrame : public brls::Box {
  public:
    explicit ShellFrame(const std::string& title);

    // Replaces (and deletes) the previous content view.
    void setContent(brls::View* content);
    void setStatus(const std::string& status);
    void setGuide(const std::string& text);
    GuidePill* guide() { return guide_; }

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
              brls::FrameContext* ctx) override;

  private:
    std::string title_;
    std::string status_;
    brls::View* content_ = nullptr;
    GuidePill*  guide_   = nullptr;
    WsToastHost toast_host_;
};
