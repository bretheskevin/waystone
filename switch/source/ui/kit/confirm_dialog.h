#pragma once
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include "overlay.h"

// Dialog callbacks run after the dialog is deleted, possibly after their owner: wraps `cb` so
// it is dropped (and logged) once the owner's `alive` token has expired or been cleared.
inline std::function<void()> ws_if_alive(std::weak_ptr<bool> alive, const char* what,
                                         std::function<void()> cb) {
    return [alive, what, cb] {
        auto a = alive.lock();
        if (!a || !*a) {
            printf("[ui] %s: owner gone, callback dropped\n", what);
            return;
        }
        if (cb) cb();
    };
}

// Yes/no dialog: the `confirm` button closes then runs on_confirm; B (WsAction::Cancel)
// closes then runs on_cancel. Callbacks run after the dialog is popped and deleted.
class ConfirmDialog : public DialogCard {
  public:
    ConfirmDialog(std::string title, std::string detail, std::function<void()> on_confirm,
                  std::function<void()> on_cancel = {}, WsAction confirm = WsAction::Confirm,
                  const char* confirm_label = nullptr, const char* cancel_label = nullptr);

  protected:
    void buildBody(brls::Box* body) override;

  private:
    std::string           title_;
    std::string           detail_;
    std::function<void()> on_confirm_;
    std::function<void()> on_cancel_;
    WsAction              confirm_;
};
