#pragma once
#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>

class Screen {
public:
    virtual ~Screen() {}
    virtual void draw_top(C3D_RenderTarget* target) = 0;
    virtual void draw_bottom(C3D_RenderTarget* target) = 0;
    virtual void handle_input(u32 kDown, touchPosition touch) = 0;
    virtual void poll() {}  // override for screens with background workers
};
