#pragma once
#include "restore_list_panel.h"
#include "snapshots_controller.h"

// Local safety snapshots of one game (stacked over the game panel, not dimmed). Owns ctrl.
class SnapshotsPanel : public ControllerRestorePanel<SnapshotsController> {
  public:
    explicit SnapshotsPanel(SnapshotsController* ctrl);

  protected:
    size_t entryCount() const override;
    std::vector<RowText> rows() const override;
    std::string confirmTitle() const override;
    std::string confirmDetail(size_t i) const override;
};
