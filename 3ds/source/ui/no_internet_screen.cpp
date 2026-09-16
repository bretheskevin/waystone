#include "no_internet_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"

// Mirror the exact Switch wording; split at the sentence boundary so each
// line fits the 400 px top screen at TEXT_SM.
static std::string reason_message(NoInternetReason r) {
    return std::string(no_internet_headline(r)) + "\n" + no_internet_detail(r);
}

NoInternetScreen::NoInternetScreen(NoInternetReason reason,
                                   std::function<void()> on_success)
    : reason_(reason), on_success_(std::move(on_success)),
      base_message_(reason_message(reason)),
      message_(base_message_),
      retry_rect_()
{}

void NoInternetScreen::draw_top(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    draw_text_centered(buf, 0, 60.0f, 0.5f, TEXT_LG, CLR_WHITE,
                       "No Internet Connection", (float)SCREEN_TOP_W);

    // Draw message lines (split on '\n' for 3DS line-by-line rendering)
    float my = 110.0f;
    const float line_h = 20.0f;
    const std::string& msg = message_;
    size_t pos = 0;
    while (true) {
        size_t nl = msg.find('\n', pos);
        std::string line = (nl == std::string::npos)
                           ? msg.substr(pos)
                           : msg.substr(pos, nl - pos);
        if (!line.empty()) {
            draw_text_centered(buf, 0, my, 0.5f, TEXT_SM, CLR_TEXT_HINT,
                               line.c_str(), (float)SCREEN_TOP_W);
        }
        my += line_h;
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

void NoInternetScreen::draw_bottom(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    float btn_w = 120.0f;
    float btn_h = 28.0f;
    float btn_x = ((float)SCREEN_BOT_W - btn_w) / 2.0f;
    float btn_y = ((float)SCREEN_BOT_H - btn_h) / 2.0f - 20.0f;
    retry_rect_ = draw_button(buf, btn_x, btn_y, btn_w, btn_h,
                              "Retry", ButtonStyle::PRIMARY, /*focused=*/true);
    draw_footer_hint(buf, "A: Retry  B: Exit");
}

void NoInternetScreen::handle_input(u32 kDown, touchPosition touch) {
    bool retry = (kDown & KEY_A) != 0;
    if (!retry && (touch.px != 0 || touch.py != 0)) {
        retry = retry_rect_.contains((float)touch.px, (float)touch.py);
    }

    if (retry) {
        if (network_available()) {
            // CRITICAL UAF guard: move callback to a local BEFORE pop_screen(),
            // which deletes 'this'. Never touch members after pop.
            std::function<void()> cb = std::move(on_success_);
            App::instance().pop_screen();
            if (cb) cb();
            return;
        } else {
            message_ = base_message_ + "\n" + no_internet_retry_suffix();
        }
    }

    if (kDown & KEY_B) {
        App::instance().quit();
    }
}
