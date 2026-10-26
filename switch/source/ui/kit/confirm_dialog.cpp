#include "confirm_dialog.h"
#include <cstdio>

ConfirmDialog::ConfirmDialog(std::string title, std::string detail, std::function<void()> on_confirm,
                             std::function<void()> on_cancel, WsAction confirm, const char* confirm_label,
                             const char* cancel_label)
    : title_(std::move(title)), detail_(std::move(detail)), on_confirm_(std::move(on_confirm)),
      on_cancel_(std::move(on_cancel)), confirm_(confirm) {
    setHint(ws_hint(confirm, confirm_label) + ws_hint_style().item_sep + ws_hint(WsAction::Cancel, cancel_label));
}

void ConfirmDialog::buildBody(brls::Box* body) {
    addTitle(body, title_);
    if (!detail_.empty()) addDetail(body, detail_);

    root()->registerAction(ws_label(confirm_), ws_brls(confirm_), [this](brls::View*) {
        if (isClosing()) return true;
        printf("[ui] confirm '%s': confirmed\n", title_.c_str());
        close(on_confirm_);
        return true;
    });
    root()->registerAction(ws_label(WsAction::Cancel), ws_brls(WsAction::Cancel), [this](brls::View*) {
        if (isClosing()) return true;
        printf("[ui] confirm '%s': cancelled\n", title_.c_str());
        close(on_cancel_);
        return true;
    });
}
