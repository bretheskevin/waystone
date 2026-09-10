#pragma once
#include "screen.h"
#include "wizard.h"
#include "session.h"
#include "vault_helpers.h"
#include <3ds.h>
#include <atomic>

class SetupScreen : public Screen {
public:
    SetupScreen(Session* session);
    ~SetupScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session* session_; Wizard wizard_;
    bool creating_; std::atomic<bool> vault_done_;
    Thread vault_thread_; VaultCreateResult vault_result_;
    static const size_t FIELD_SERVER=0, FIELD_USERNAME=1, FIELD_PASSWORD=2, FIELD_PASSPHRASE=3, FIELD_CONFIRM=4, NUM_VALUES=5;
    bool validate_step(size_t step);
    void on_finish();
    void do_create_vault();
    static void vault_thread_entry(void* arg);
    SetupScreen(const SetupScreen&); SetupScreen& operator=(const SetupScreen&);
};
