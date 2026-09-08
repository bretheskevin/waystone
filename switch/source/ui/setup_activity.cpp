#include "setup_activity.h"
#include "swkbd_util.h"
#include "title_list_activity.h"
#include "sync_controller.h"
#include <cstdio>
#include <sys/stat.h>

extern "C" {
struct Vault;
#include "waystone.h"
}

SetupActivity::SetupActivity(Session* session) : session_(session) {}
SetupActivity::~SetupActivity() = default;

brls::View* SetupActivity::createContentView()
{
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(20.0f);

    auto* title = new brls::Label();
    title->setText("Waystone Setup");
    title->setFontSize(28.0f);
    title->setSingleLine(true);
    col->addView(title);

    status_label_ = new brls::Label();
    status_label_->setText("Press A to begin setup");
    status_label_->setFontSize(20.0f);
    col->addView(status_label_);

    frame->setContentView(col);
    return frame;
}

void SetupActivity::onContentAvailable()
{
    registerAction("Setup", brls::BUTTON_A, [this](brls::View*) { run_setup(); return true; });
}

void SetupActivity::run_setup()
{
    std::string server_url = swkbd_prompt("Server URL", "https://", false);
    if (server_url.empty()) { status_label_->setText("Setup cancelled (no server URL)"); return; }

    std::string username = swkbd_prompt("Username", "", false);
    if (username.empty()) { status_label_->setText("Setup cancelled (no username)"); return; }

    std::string password = swkbd_prompt("WebDAV Password", "", true);
    if (password.empty()) {
        status_label_->setText("Setup cancelled (no password)");
        zeroize_string(password);
        return;
    }

    std::string passphrase = swkbd_prompt("Vault Passphrase", "", true);
    if (passphrase.empty()) {
        status_label_->setText("Setup cancelled (no passphrase)");
        zeroize_string(password);
        return;
    }

    std::string confirm = swkbd_prompt("Confirm Passphrase", "", true);
    if (confirm != passphrase) {
        status_label_->setText("Passphrases do not match");
        zeroize_string(password);
        zeroize_string(passphrase);
        zeroize_string(confirm);
        return;
    }
    zeroize_string(confirm);

    status_label_->setText("Creating vault...");

    const char* keys_path = "sdmc:/waystone/keys.json";
    FILE* existing = fopen(keys_path, "rb");
    if (existing) {
        fseek(existing, 0, SEEK_END);
        long esz = ftell(existing);
        fclose(existing);
        if (esz > 0) {
            status_label_->setText("keys.json already exists! Aborting setup.");
            zeroize_string(password);
            zeroize_string(passphrase);
            return;
        }
        // 0-byte file: treat as no vault and proceed (partial write from a failed setup).
    }

    WsBuf recovery = {nullptr, 0};
    WsBuf keys = {nullptr, 0};
    WsVault* vault = ws_vault_init(passphrase.c_str(), &recovery, &keys);
    zeroize_string(passphrase);

    if (!vault) {
        const char* err = ws_last_error();
        status_label_->setText(std::string("Vault creation failed: ") + (err ? err : "unknown"));
        zeroize_string(password);
        ws_buf_free(recovery);
        ws_buf_free(keys);
        return;
    }

    mkdir("sdmc:/waystone", 0755);
    FILE* wf = fopen(keys_path, "wb");
    if (wf) {
        fwrite(keys.ptr, 1, keys.len, wf);
        fclose(wf);
    }
    ws_buf_free(keys);

    session_->config.server_url = server_url;
    session_->config.username = username;
    wsconfig_save(session_->config, session_->config_path.c_str());

    session_->vault = vault;
    session_->dav.server_url = server_url;
    session_->dav.user = username;
    session_->dav.pass = password;
    zeroize_string(password);

    if (recovery.ptr && recovery.len > 0) {
        std::string recovery_path = "sdmc:/waystone/recovery-" + session_->device_id + ".txt";
        FILE* rf = fopen(recovery_path.c_str(), "wb");
        if (rf) {
            fwrite(recovery.ptr, 1, recovery.len, rf);
            fclose(rf);
        }
        // brls::Dialog not available; notify user the key was saved to SD.
        brls::Application::notify("Recovery key saved to: " + recovery_path);
    }
    ws_buf_free(recovery);

    auto titles = list_titles();
    auto* ctrl = new SyncController(session_->vault, session_->uid, session_->device_id,
                                    session_->dav.as_cfg(), std::move(titles));
    brls::Application::pushActivity(new TitleListActivity(ctrl, session_));
}
