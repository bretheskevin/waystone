#pragma once
#include <borealis.hpp>
#include <string>

// Shows a transient bottom-left toast; a new toast replaces the current one. Main thread only.
void ws_toast(const std::string& text);

// Paints the current toast (if any). Called last by every toast host's draw.
void ws_toast_draw(NVGcontext* vg);

// Views that paint the toast (ShellFrame, overlay roots) own one of these so only the
// top-most live host paints it (no double-darkening under translucent overlays).
class WsToastHost {
  public:
    WsToastHost();
    ~WsToastHost();
    WsToastHost(const WsToastHost&)            = delete;
    WsToastHost& operator=(const WsToastHost&) = delete;

    void draw(NVGcontext* vg);
};
