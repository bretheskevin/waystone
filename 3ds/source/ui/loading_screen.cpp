#include "loading_screen.h"
#include "title_list_screen.h"
#include "unlock_screen.h"
#include "app.h"
#include "theme.h"
#include "widgets.h"
#include "worker_thread.h"
#include <cstdio>
#include <cmath>

struct Vault;
extern "C" {
#include "waystone.h"
}
#include "session_store.h"

LoadingScreen::LoadingScreen(Session* session, bool auto_unlock,
                             uint8_t* keys_data, size_t keys_len)
    : session_(session), auto_unlock_(auto_unlock),
      keys_data_(keys_data), keys_len_(keys_len),
      worker_thread_(0), handled_(false), spinner_angle_(0.0f)
{
    worker_thread_ = start_worker_thread(worker_entry, this);
}

LoadingScreen::~LoadingScreen() {
    if (worker_thread_) {
        threadJoin(worker_thread_, U64_MAX);
        threadFree(worker_thread_);
    }
}

void LoadingScreen::worker_entry(void* arg) {
    LoadingScreen* self = static_cast<LoadingScreen*>(arg);

    if (self->auto_unlock_) {
        std::string webdav_pass;
        WsVault* v = session_store_load_vault(webdav_pass);
        if (!v) {
            self->ctx_.result.success = false;
            self->ctx_.result.error   = "session expired";
            self->ctx_.done.store(true);
            return;
        }
        self->session_->vault          = v;
        self->session_->dav.server_url = self->session_->config.server_url;
        self->session_->dav.user       = self->session_->config.username;
        self->session_->dav.pass       = webdav_pass;
        zeroize_string(webdav_pass);
    }

    self->ctx_.result.titles  = list_titles();
    self->ctx_.result.success = true;
    self->ctx_.done.store(true);
}

void LoadingScreen::draw_top(C3D_RenderTarget* /*target*/) {
    C2D_TextBuf buf = App::instance().text_buf();
    draw_text_centered(buf, SCREEN_TOP_W / 2.0f, 88.0f, 0.5f,
                       TEXT_LG, CLR_TEXT, "Loading...", SCREEN_TOP_W);
    draw_spinner(buf, SCREEN_TOP_W / 2.0f, 126.0f, spinner_angle_);
}

void LoadingScreen::draw_bottom(C3D_RenderTarget* /*target*/) {
    draw_footer_hint(App::instance().text_buf(), "Please wait...");
}

void LoadingScreen::handle_input(u32 /*kDown*/, touchPosition /*touch*/) {}

void LoadingScreen::poll() {
    spinner_angle_ += 0.05f;
    if (!handled_ && ctx_.done.load()) {
        on_done();
    }
}

void LoadingScreen::on_done() {
    handled_ = true;
    if (worker_thread_) {
        threadJoin(worker_thread_, U64_MAX);
        threadFree(worker_thread_);
        worker_thread_ = 0;
    }

    if (ctx_.result.success) {
        // set_screen deletes 'this' — no member access after this point
        App::instance().set_screen(
            new TitleListScreen(session_, ctx_.result.titles));
        return;
    }

    // Failure path
    printf("[loading] worker failed: %s\n", ctx_.result.error.c_str());
    if (auto_unlock_) {
        session_store_clear();
        if (keys_data_ && keys_len_ > 0) {
            App::instance().set_screen(
                new UnlockScreen(session_, keys_data_, keys_len_));
        } else {
            App::instance().pop_screen();
        }
    } else {
        App::instance().pop_screen();
    }
}
