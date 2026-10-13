#include "applet_mode_activity.h"
#include "keymap_switch.h"
#include <cstdio>

brls::View* AppletModeActivity::createContentView() {
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setPadding(60.0f);
    col->setAlignItems(brls::AlignItems::CENTER);
    col->setJustifyContent(brls::JustifyContent::CENTER);
    col->setGrow(1.0f);

    auto* title = new brls::Label();
    title->setText("Full Application Mode Required");
    title->setFontSize(32.0f);
    title->setSingleLine(true);
    title->setMargins(0.0f, 0.0f, 24.0f, 0.0f);
    col->addView(title);

    auto* msg = new brls::Label();
    msg->setText(
        "Waystone is running as an applet, which does not have enough\n"
        "memory to mount game saves.\n"
        "\n"
        "To launch it properly:\n"
        "  1. Close Waystone.\n"
        "  2. Hold R and start any game \xe2\x80\x94 keep holding R until\n"
        "     the Homebrew Menu appears.\n"
        "  3. Start Waystone from the Homebrew Menu.");
    msg->setFontSize(20.0f);
    msg->setTextColor(nvgRGB(0x71, 0x71, 0x7A));
    msg->setMargins(0.0f, 0.0f, 40.0f, 0.0f);
    col->addView(msg);

    auto* exit_btn = new brls::Button();
    exit_btn->setText(ws_label(WsAction::ExitApp));
    exit_btn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    exit_btn->setFocusable(true);
    exit_btn->registerClickAction([](brls::View*) {
        printf("[ui] applet-mode: exit app\n");
        brls::Application::quit();
        return true;
    });
    col->addView(exit_btn);

    return col;
}

void AppletModeActivity::onContentAvailable() {
    registerAction(ws_label(WsAction::ExitApp), ws_brls(WsAction::ExitApp), [](brls::View*) {
        printf("[ui] applet-mode: exit app\n");
        brls::Application::quit();
        return true;
    });
}
