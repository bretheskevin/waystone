#include "recovery_screen.h"
#include "title_list_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"

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
    draw_text_centered(buf, 0, 40.0f, 0.5f, TEXT_2XL,  CLR_WHITE,        "Recovery Key",   (float)SCREEN_TOP_W);
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
    draw_text_centered(buf, 0, 16.0f, 0.5f, TEXT_BASE, CLR_TEXT_HINT,
                       "Your recovery key:", (float)SCREEN_BOT_W);
    draw_text_centered(buf, 0, 36.0f, 0.5f, TEXT_SM,   CLR_TEXT,
                       recovery_hex_.c_str(), (float)SCREEN_BOT_W);
    std::string path_line = "Saved to: " + recovery_path_;
    draw_text_centered(buf, 0, 96.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT,
                       path_line.c_str(), (float)SCREEN_BOT_W);
    float btn_w = 160.0f, btn_h = 32.0f;
    float btn_x = ((float)SCREEN_BOT_W - btn_w) / 2.0f;
    draw_button(buf, btn_x, 130.0f, btn_w, btn_h, "I've saved it", ButtonStyle::PRIMARY, true);
    draw_footer_hint(buf, "A: Confirm");
}

void RecoveryScreen::handle_input(u32 kDown, touchPosition touch) {
    bool confirm = false;
    if (kDown & KEY_A) confirm = true;
    if (!confirm && (touch.px != 0 || touch.py != 0)) {
        float btn_w = 160.0f, btn_h = 32.0f;
        float btn_x = ((float)SCREEN_BOT_W - btn_w) / 2.0f;
        Rect btn = {btn_x, 130.0f, btn_w, btn_h};
        if (btn.contains((float)touch.px, (float)touch.py)) confirm = true;
    }
    if (confirm) {
        // set_screen deletes 'this'
        App::instance().set_screen(new TitleListScreen(session_));
    }
}
