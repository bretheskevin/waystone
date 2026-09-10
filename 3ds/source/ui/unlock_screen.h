#pragma once
#include "screen.h"
#include "wizard.h"
#include "session.h"
#include <3ds.h>
#include <atomic>
#include <string>

class UnlockScreen : public Screen {
public:
    // Borrows keys_data for the lifetime of this screen; does NOT free it.
    UnlockScreen(Session* session, uint8_t* keys_data, size_t keys_len);
    ~UnlockScreen();
    void draw_top(C3D_RenderTarget* target);
    void draw_bottom(C3D_RenderTarget* target);
    void handle_input(u32 kDown, touchPosition touch);
    void poll();
private:
    Session*  session_;
    uint8_t*  keys_data_;
    size_t    keys_len_;
    bool      recovery_mode_;
    Wizard    wizard_;
    bool      unlocking_;
    std::atomic<bool> unlock_done_;
    Thread    unlock_thread_;
    bool      unlock_success_;
    std::string unlock_error_;
    static const size_t FIELD_PASSPHRASE = 0, FIELD_PASSWORD = 1, NUM_VALUES = 2;
    std::vector<WizardStepDef> make_steps() const;
    bool validate();
    void do_unlock();
    static void unlock_thread_entry(void* arg);
    UnlockScreen(const UnlockScreen&);
    UnlockScreen& operator=(const UnlockScreen&);
};
