#include "no_internet_activity.h"
#include "net_status.h"

static const char* reason_message(NoInternetReason r) {
    switch (r) {
        case NoInternetReason::NoNetwork:
            return "No internet connection detected. "
                   "Check your Wi-Fi settings and try again.";
        case NoInternetReason::ServerUnreachable:
            return "Can't reach your Waystone server. "
                   "Check the server URL in Settings and try again.";
    }
    return "";
}

NoInternetActivity::NoInternetActivity(NoInternetReason reason,
                                       std::function<void()> on_success)
    : reason_(reason), on_success_(std::move(on_success)) {}

brls::View* NoInternetActivity::createContentView() {
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(60.0f);
    col->setAlignItems(brls::AlignItems::CENTER);
    col->setJustifyContent(brls::JustifyContent::CENTER);
    col->setGrow(1.0f);

    auto* title = new brls::Label();
    title->setText("No Internet Connection");
    title->setFontSize(32.0f);
    title->setSingleLine(true);
    title->setMargins(0.0f, 0.0f, 24.0f, 0.0f);
    col->addView(title);

    msg_label_ = new brls::Label();
    msg_label_->setText(reason_message(reason_));
    msg_label_->setFontSize(20.0f);
    msg_label_->setTextColor(nvgRGB(0x71, 0x71, 0x7A));
    msg_label_->setMargins(0.0f, 0.0f, 40.0f, 0.0f);
    col->addView(msg_label_);

    auto* retry_btn = new brls::Button();
    retry_btn->setText("Retry");
    retry_btn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    retry_btn->setFocusable(true);
    retry_btn->registerClickAction([this](brls::View*) {
        if (network_available()) {
            auto callback = std::move(on_success_);
            brls::Application::popActivity();
            if (callback) {
                callback();
            }
        } else {
            msg_label_->setText(
                std::string(reason_message(reason_)) +
                " Still no connection. Try again.");
        }
        return true;
    });
    col->addView(retry_btn);

    return col;
}

void NoInternetActivity::onContentAvailable() {
    registerAction("Exit", brls::BUTTON_B, [](brls::View*) {
        brls::Application::quit();
        return true;
    });
}
