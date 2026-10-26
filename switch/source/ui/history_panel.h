#pragma once
#include "history_controller.h"
#include "restore_list_panel.h"

// Remote history versions of one game (stacked over the game panel, not dimmed). Owns ctrl.
class HistoryPanel : public ControllerRestorePanel<HistoryController> {
  public:
    explicit HistoryPanel(HistoryController* ctrl);

  protected:
    size_t entryCount() const override;
    std::vector<RowText> rows() const override;
    std::string confirmTitle() const override;
    std::string confirmDetail(size_t i) const override;
};
