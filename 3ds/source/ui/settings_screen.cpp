#include "settings_screen.h"
#include "unlock_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "wsconfig.h"
#include "saves.h"
#include "keys_file.h"
#include "version.h"
#include "keymap_3ds.h"
#include <cstdio>
#include <cstdlib>

struct Vault;
extern "C" {
#include "waystone.h"
}

SettingsScreen::SettingsScreen(Session* session)
    : session_(session),
      cursor_(0) {
    for (size_t i = 0; i < NUM_ROWS; i++) row_rects_[i] = Rect();
}

void SettingsScreen::poll() {
    if (update_.poll()) status_text_ = update_.status();
}

void SettingsScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    char header[64];
    snprintf(header, sizeof(header), "Waystone v%s", WS_APP_VERSION);
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, header, (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_TEXT, "Settings", (float)SCREEN_TOP_W);
    if (!status_text_.empty()) {
        u32 clr = update_.status_color();
        draw_text_centered(buf, 0, 80.0f, 0.5f, TEXT_BASE, clr, status_text_.c_str(), (float)SCREEN_TOP_W);
    }
}

void SettingsScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    float x = SP_MD;
    float w = (float)SCREEN_BOT_W - 2.0f * SP_MD;
    float row_h = 22.0f;  // 8 rows + device line must clear the footer hint
    float y = (float)SP_MD;

    // Cleared each frame: while a banner replaces the row list there is nothing to tap.
    for (size_t i = 0; i < NUM_ROWS; i++) row_rects_[i] = Rect();

    if (update_.draw_banner(buf)) return;

    const char* policy_str = (session_->config.conflict_policy == WsConflictPolicy::Prompt)
        ? "Prompt" : "Newest Wins";
    char row_labels[NUM_ROWS][128];
    snprintf(row_labels[0], sizeof(row_labels[0]), "Server: %s", session_->config.server_url.c_str());
    snprintf(row_labels[1], sizeof(row_labels[1]), "Username: %s", session_->config.username.c_str());
    snprintf(row_labels[2], sizeof(row_labels[2]), "%s", webdav_pass_label(session_->dav.pass).c_str());
    snprintf(row_labels[3], sizeof(row_labels[3]), "Conflict: %s", policy_str);
    snprintf(row_labels[4], sizeof(row_labels[4]), "Safety Backup: %s", session_->config.safety_backup ? "ON" : "OFF");
    snprintf(row_labels[5], sizeof(row_labels[5]), "Check for updates");
    snprintf(row_labels[6], sizeof(row_labels[6]), "Save");
    snprintf(row_labels[7], sizeof(row_labels[7]), "Log out");

    auto draw_row = [&](size_t i) {
        row_rects_[i] = { x, y, w, row_h };
        bool focused = (cursor_ == i);
        if (focused) draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2, RAD_SM + 1, CLR_ACCENT);
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, focused ? CLR_PRIMARY_50 : CLR_CARD_BG);
        draw_text(buf, x + SP_MD, y + SP_XS + 1.0f, 0.51f, TEXT_BASE,
                  i == ROW_LOG_OUT ? CLR_ERROR : CLR_TEXT, row_labels[i]);
        y += row_h + SP_XS;
    };
    for (size_t i = 0; i < FIRST_ACTION_ROW; i++) draw_row(i);

    char dev_buf[128];
    snprintf(dev_buf, sizeof(dev_buf), "Device: %s", session_->device_id.c_str());
    draw_text(buf, x + SP_SM, y + 2.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT, dev_buf);
    y += 16.0f;

    for (size_t i = FIRST_ACTION_ROW; i < NUM_ROWS; i++) draw_row(i);

    static const WsAction row_acts[] = { WsAction::Select, WsAction::Back };
    draw_footer_hint(buf, (ws_hint_bar(row_acts) + "  DPad: Nav").c_str());
}

void SettingsScreen::handle_input(u32 kDown, touchPosition touch) {

    if (update_.handle_input(kDown)) return;

    if (kDown & KEY_DUP)   { cursor_ = (cursor_ == 0) ? NUM_ROWS - 1 : cursor_ - 1; }
    if (kDown & KEY_DDOWN) { cursor_ = (cursor_ + 1) % NUM_ROWS; }
    if (kDown & ws_key(WsAction::Back)) {
        printf("[ui] settings back\n");
        update_.release();  // instant pop: dtor never joins a live worker
        App::instance().pop_screen();
        return;
    }
    // ---- Touch: tap a row to focus + activate it (banners stay key-driven) ----
    if (touch.px != 0 || touch.py != 0) {
        for (size_t i = 0; i < NUM_ROWS; i++) {
            if (row_rects_[i].contains((float)touch.px, (float)touch.py)) {
                printf("[ui] settings touch row=%zu\n", i);
                cursor_ = i;
                activate_row(i);  // may delete this screen (log out) -- do not touch members after
                return;
            }
        }
    }

    if (kDown & ws_key(WsAction::Select)) {
        activate_row(cursor_);  // may delete this screen (log out) -- do not touch members after
    }
}

void SettingsScreen::activate_row(size_t row) {
    switch (row) {
        case 0: {
            std::string v = swkbd_prompt("Server URL", false, session_->config.server_url);
            if (!v.empty()) {
                session_->config.server_url = v;
                session_->dav.server_url = v;
                zeroize_string(session_->dav.pass);
            }
            break;
        }
        case 1: {
            std::string v = swkbd_prompt("Username", false, session_->config.username);
            if (!v.empty()) {
                session_->config.username = v;
                session_->dav.user = v;
            }
            break;
        }
        case 2: {
            std::string v = swkbd_prompt("WebDAV password", true, "");
            if (update_webdav_pass(session_->vault, session_->dav.pass, v)) status_text_ = "WebDAV password updated";
            break;
        }
        case 3: {
            session_->config.conflict_policy =
                (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
                    ? WsConflictPolicy::Prompt : WsConflictPolicy::NewestWins;
            break;
        }
        case 4: {
            session_->config.safety_backup = !session_->config.safety_backup;
            break;
        }
        case 5: {
            update_.start_check("settings");
            break;
        }
        case 6: {
            printf("[ui] settings save -> %s\n", session_->config_path.c_str());
            if (wsconfig_save(session_->config, session_->config_path.c_str())) {
                printf("[ui] settings save ok\n");
                status_text_ = "Settings saved";
            } else {
                printf("[ui] settings save FAILED\n");
                status_text_ = "Failed to save settings";
            }
            break;
        }
        case ROW_LOG_OUT: {
            printf("[ui] settings: log out\n");
            // set_screen/pop_screen below delete this screen; detach first so
            // ~SettingsScreen never joins a running worker on the render thread.
            update_.release();
            session_drop_login(session_->vault, session_->dav.user, session_->dav.pass);
            long klen = 0;
            uint8_t* kbuf = read_keys_file(WS_KEYS_PATH, &klen);
            if (kbuf && klen > 0) {
                // kbuf intentionally not freed: UnlockScreen borrows it for app lifetime
                App::instance().set_screen(new UnlockScreen(session_, kbuf, static_cast<size_t>(klen)));
                return;
            }
            App::instance().pop_screen();
            return;
        }
    }
}
