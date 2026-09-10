#include "app.h"
#include "theme.h"
#include <cstdio>

App* App::instance_ = 0;

App::App() : top_target_(0), bot_target_(0), text_buf_(0), running_(true), screen_changed_(false) {
    instance_ = this;
    top_target_ = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bot_target_ = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    text_buf_ = C2D_TextBufNew(8192);
}
App::~App() {
    for (size_t i=0;i<stack_.size();i++) delete stack_[i];
    stack_.clear();
    if (text_buf_) C2D_TextBufDelete(text_buf_);
    instance_ = 0;
}
App& App::instance() { return *instance_; }
void App::set_screen(Screen* s) {
    for (size_t i=0;i<stack_.size();i++) delete stack_[i];
    stack_.clear(); stack_.push_back(s);
    screen_changed_ = true;
}
void App::push_screen(Screen* s) { stack_.push_back(s); screen_changed_ = true; }
void App::pop_screen() { if (!stack_.empty()) { delete stack_.back(); stack_.pop_back(); screen_changed_ = true; } }
void App::quit() { running_ = false; }
void App::run() {
    while (running_ && aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        touchPosition touch; hidTouchRead(&touch);
        if (kDown & KEY_START) { quit(); break; }
        if (!stack_.empty()) stack_.back()->poll();
        if (screen_changed_) {
            screen_changed_ = false;
        } else if (!stack_.empty()) {
            stack_.back()->handle_input(kDown, touch);
        }
        C2D_TextBufClear(text_buf_);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top_target_, CLR_BG_TOP);
        C2D_SceneBegin(top_target_);
        if (!stack_.empty()) stack_.back()->draw_top(top_target_);
        C2D_TargetClear(bot_target_, CLR_BG_BOTTOM);
        C2D_SceneBegin(bot_target_);
        if (!stack_.empty()) stack_.back()->draw_bottom(bot_target_);
        C3D_FrameEnd(0);
    }
}
