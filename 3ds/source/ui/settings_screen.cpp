#include "settings_screen.h"
#include "unlock_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include "swkbd_util.h"
#include "session_store.h"
#include "wsconfig.h"
#include "saves.h"
#include <cstdio>
#include <cstdlib>

struct Vault;
extern "C" {
#include "waystone.h"
}

SettingsScreen::SettingsScreen(Session* session)
    : session_(session), cursor_(0) {}

void SettingsScreen::draw_top(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    draw_text_centered(buf, 0, 10.0f, 0.5f, TEXT_SM, CLR_NEUTRAL_400, "Waystone", (float)SCREEN_TOP_W);
    draw_text_centered(buf, 0, 30.0f, 0.5f, TEXT_XL, CLR_TEXT, "Settings", (float)SCREEN_TOP_W);
    if (!status_text_.empty()) {
        draw_text_centered(buf, 0, 80.0f, 0.5f, TEXT_BASE, CLR_NEUTRAL_400, status_text_.c_str(), (float)SCREEN_TOP_W);
    }
}

void SettingsScreen::draw_bottom(C3D_RenderTarget* target) {
    (void)target;
    C2D_TextBuf buf = App::instance().text_buf();
    float x = SP_MD;
    float w = (float)SCREEN_BOT_W - 2.0f * SP_MD;
    float row_h = 28.0f;
    float y = (float)SP_MD;

    const char* policy_str = (session_->config.conflict_policy == WsConflictPolicy::Prompt)
        ? "Prompt" : "Newest Wins";
    char row_labels[6][128];
    snprintf(row_labels[0], sizeof(row_labels[0]), "Server: %s", session_->config.server_url.c_str());
    snprintf(row_labels[1], sizeof(row_labels[1]), "Username: %s", session_->config.username.c_str());
    snprintf(row_labels[2], sizeof(row_labels[2]), "Conflict: %s", policy_str);
    snprintf(row_labels[3], sizeof(row_labels[3]), "Safety Backup: %s", session_->config.safety_backup ? "ON" : "OFF");
    snprintf(row_labels[4], sizeof(row_labels[4]), "Save");
    snprintf(row_labels[5], sizeof(row_labels[5]), "Log out");

    for (size_t i = 0; i < 4; i++) {
        bool focused = (cursor_ == i);
        u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        if (focused) {
            draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2, RAD_SM + 1, CLR_ACCENT);
        }
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        draw_text(buf, x + SP_MD, y + SP_SM, 0.51f, TEXT_BASE, CLR_TEXT, row_labels[i]);
        y += row_h + SP_XS;
    }

    char dev_buf[128];
    snprintf(dev_buf, sizeof(dev_buf), "Device: %s", session_->device_id.c_str());
    draw_text(buf, x + SP_SM, y + 2.0f, 0.5f, TEXT_SM, CLR_TEXT_HINT, dev_buf);
    y += 20.0f;

    for (size_t i = 4; i < NUM_ROWS; i++) {
        bool focused = (cursor_ == i);
        u32 bg = focused ? CLR_PRIMARY_50 : CLR_CARD_BG;
        u32 text_color = (i == 5) ? CLR_ERROR : CLR_TEXT;
        if (focused) {
            draw_rounded_rect(x - 1, y - 1, 0.49f, w + 2, row_h + 2, RAD_SM + 1, CLR_ACCENT);
        }
        draw_rounded_rect(x, y, 0.5f, w, row_h, RAD_SM, bg);
        draw_text(buf, x + SP_MD, y + SP_SM, 0.51f, TEXT_BASE, text_color, row_labels[i]);
        y += row_h + SP_XS;
    }

    draw_footer_hint(buf, "A: Select  B: Back  DPad: Nav");
}

void SettingsScreen::handle_input(u32 kDown, touchPosition touch) {
    (void)touch;
    if (kDown & KEY_DUP)   { cursor_ = (cursor_ == 0) ? NUM_ROWS - 1 : cursor_ - 1; }
    if (kDown & KEY_DDOWN) { cursor_ = (cursor_ + 1) % NUM_ROWS; }
    if (kDown & KEY_B) { App::instance().pop_screen(); return; }
    if (kDown & KEY_A) {
        switch (cursor_) {
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
                session_->config.conflict_policy =
                    (session_->config.conflict_policy == WsConflictPolicy::NewestWins)
                        ? WsConflictPolicy::Prompt : WsConflictPolicy::NewestWins;
                break;
            }
            case 3: {
                session_->config.safety_backup = !session_->config.safety_backup;
                break;
            }
            case 4: {
                if (wsconfig_save(session_->config, session_->config_path.c_str()))
                    status_text_ = "Settings saved";
                else
                    status_text_ = "Failed to save settings";
                break;
            }
            case 5: {
                session_store_clear();
                if (session_->vault) { ws_vault_free(session_->vault); session_->vault = 0; }
                zeroize_string(session_->dav.pass);
                zeroize_string(session_->dav.user);
                long klen = 0;
                uint8_t* kbuf = read_keys_file("sdmc:/waystone/keys.json", &klen);
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
}
