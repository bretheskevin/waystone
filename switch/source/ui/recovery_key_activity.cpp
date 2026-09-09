#include "recovery_key_activity.h"
#include "vault_helpers.h"

RecoveryKeyActivity::RecoveryKeyActivity(Session* session,
                                         const std::string& recovery_hex,
                                         const std::string& recovery_path)
    : session_(session), recovery_hex_(recovery_hex), recovery_path_(recovery_path) {}

RecoveryKeyActivity::~RecoveryKeyActivity() {
    zeroize_string(recovery_hex_);
}

brls::View* RecoveryKeyActivity::createContentView() {
    auto* frame = new brls::ScrollingFrame();
    frame->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    frame->setGrow(1.0f);

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(40.0f);
    col->setAlignItems(brls::AlignItems::CENTER);

    auto* title = new brls::Label();
    title->setText("Recovery Key");
    title->setFontSize(32.0f);
    title->setSingleLine(true);
    title->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    col->addView(title);

    auto* key_label = new brls::Label();
    key_label->setText(recovery_hex_);
    key_label->setFontSize(20.0f);
    key_label->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    col->addView(key_label);

    auto* path_label = new brls::Label();
    path_label->setText("Saved to: " + recovery_path_);
    path_label->setFontSize(18.0f);
    path_label->setTextColor(nvgRGB(0x71, 0x71, 0x7A));
    path_label->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    col->addView(path_label);

    auto* warning = new brls::Label();
    warning->setText("Write this key down or keep the file on your SD card. "
                     "If you forget your passphrase, this is the ONLY way to recover your vault.");
    warning->setFontSize(18.0f);
    warning->setTextColor(nvgRGB(0xEF, 0x44, 0x44));
    warning->setMargins(0.0f, 0.0f, 24.0f, 0.0f);
    col->addView(warning);

    auto* btn = new brls::Button();
    btn->setText("I've saved it");
    btn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    btn->setFocusable(true);
    btn->registerClickAction([this](brls::View*) {
        push_dashboard(session_);
        return true;
    });
    col->addView(btn);

    frame->setContentView(col);
    return frame;
}

void RecoveryKeyActivity::onContentAvailable() {
    // No B action — user must confirm via the button.
}
