#include "recovery_screen.h"
#include "loading_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include "keymap_3ds.h"
#include <cstdio>

RecoveryScreen::RecoveryScreen(Session* session,
                               const std::string& recovery_hex,
                               const std::string& recovery_path)
    : session_(session), recovery_hex_(recovery_hex), recovery_path_(recovery_path) {}

RecoveryScreen::~RecoveryScreen() {
    zeroize_string(recovery_hex_);
}

void RecoveryScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM,   CLR_NEUTRAL_400, "Waystone",        (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 40.0f, 0.5f, TEXT_2XL,  CLR_TEXT,        "Recovery Key",   (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 80.0f, 0.5f, TEXT_BASE,  CLR_ERROR,
        "Write this down or keep it on your SD card.",   (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 100.0f, 0.5f, TEXT_BASE, CLR_ERROR,
        "If you forget your passphrase, this is the",    (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 116.0f, 0.5f, TEXT_BASE, CLR_ERROR,
        "ONLY way to recover your vault.",               (float)SCREEN_TOP_W);
}

void RecoveryScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    float area_w = (float)SCREEN_BOT_W - 2.0f * (float)SP_MD;
    draw_text_centered(buf, 0, 12.0f, 0.5f, TEXT_BASE, CLR_TEXT_HINT,
                       "Your recovery key:", (float)SCREEN_BOT_W);
    // Wrap the key across lines so a long hex string stays on-screen and readable.
    draw_text_wrapped_centered(buf, (float)SP_MD, 32.0f, 0.51f, TEXT_SM, CLR_TEXT,
                               recovery_hex_.c_str(), area_w, 15.0f);
    std::string path_line = "Saved to: " + recovery_path_;
    draw_text_centered_fit(buf, (float)SP_MD, 96.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT,
                           path_line.c_str(), area_w, TEXT_SM * 0.6f);
    float btn_w = 160.0f, btn_h = 32.0f;
    float btn_x = ((float)SCREEN_BOT_W - btn_w) / 2.0f;
    draw_button(buf, btn_x, 130.0f, btn_w, btn_h, "I've saved it", ButtonStyle::PRIMARY, true);
    draw_footer_hint(buf, ws_hint(WsAction::Continue).c_str());
}

void RecoveryScreen::handle_input(u32 kDown, touchPosition touch) {
    bool confirm = false;
    if (kDown & ws_key(WsAction::Continue)) confirm = true;
    if (!confirm && (touch.px != 0 || touch.py != 0)) {
        float btn_w = 160.0f, btn_h = 32.0f;
        float btn_x = ((float)SCREEN_BOT_W - btn_w) / 2.0f;
        Rect btn = {btn_x, 130.0f, btn_w, btn_h};
        if (btn.contains((float)touch.px, (float)touch.py)) confirm = true;
    }
    if (confirm) {
        printf("[ui] recovery key acknowledged\n");
        // set_screen deletes 'this'
        // list_titles runs off the render thread inside LoadingScreen
        App::instance().set_screen(new LoadingScreen(session_, false));
    }
}
