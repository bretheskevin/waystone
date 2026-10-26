// kit-panel, kit-panel-stacked, kit-confirm: kit overlays over a ShellFrame.
#include "registry.h"
#include "kit_preview.h"
#include "kit/confirm_dialog.h"
#include "kit/menu_list.h"
#include "kit/overlay.h"
#include "kit/ws_views.h"
#include <cstdio>

class PreviewPanel : public SlidePanel {
  public:
    PreviewPanel(float width, bool dim, std::string title) : SlidePanel(width, dim), title_(std::move(title)) {
        setGuide(ws_hint_bar({WsAction::Select, WsAction::Back}));
    }

  protected:
    void buildBody(brls::Box* body) override {
        body->setPadding(WS_DIALOG_PAD, 0.0f, 0.0f, 0.0f);
        brls::Label* heading = ws_make_label(title_, WS_DIALOG_TITLE_SIZE, WS_C_TEXT);
        heading->setMarginLeft(WS_ROW_INSET);
        heading->setMarginBottom(WS_DIALOG_PAD / 2.0f);
        body->addView(heading);
        auto* menu = new MenuList();
        for (const char* text : {"Sync now", "History", "Snapshots"})
            menu->addRow(text, "", [text] { printf("[ui] preview panel row '%s' clicked\n", text); });
        body->addView(menu);
    }

  private:
    std::string title_;
};

static void run_panel(Session&) {
    brls::Application::pushActivity(new PreviewShellActivity(PREVIEW_READY_DELAY_MS, [] {
        OverlayActivity::push(new PreviewPanel(420.0f, true, "Animal Crossing"));
    }));
}

static void run_panel_stacked(Session&) {
    brls::Application::pushActivity(new PreviewShellActivity(PREVIEW_READY_DELAY_MS, [] {
        OverlayActivity::push(new PreviewPanel(420.0f, true, "Animal Crossing"));
        OverlayActivity::push(new PreviewPanel(640.0f, false, "History"));
    }));
}

static void run_confirm(Session&) {
    brls::Application::pushActivity(new PreviewShellActivity(PREVIEW_READY_DELAY_MS, [] {
        OverlayActivity::push(new ConfirmDialog(
            "Keep local save?", "Overwrites the server copy of Animal Crossing.",
            [] { printf("[ui] preview: keep local confirmed\n"); }, {}, WsAction::KeepLocal));
    }));
}

static PreviewModeReg reg_panel("kit-panel", &run_panel);
static PreviewModeReg reg_panel_stacked("kit-panel-stacked", &run_panel_stacked);
static PreviewModeReg reg_confirm("kit-confirm", &run_confirm);
