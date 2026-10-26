#pragma once
#include <functional>
#include <string>
#include "overlay.h"

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
