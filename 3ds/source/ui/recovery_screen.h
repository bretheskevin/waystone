#pragma once
#include "screen.h"
#include "session.h"
#include <string>

class RecoveryScreen : public Screen {
public:
    RecoveryScreen(Session* session, const std::string& recovery_hex,
                   const std::string& recovery_path);
    ~RecoveryScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
private:
    Session*    session_;
    std::string recovery_hex_;
    std::string recovery_path_;
    RecoveryScreen(const RecoveryScreen&);
    RecoveryScreen& operator=(const RecoveryScreen&);
};
