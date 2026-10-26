#include "snapshots_panel.h"
#include <cstdio>

SnapshotsPanel::SnapshotsPanel(SnapshotsController* ctrl, const Session* session)
    : ControllerRestorePanel(ctrl, session, "Snapshots", "No snapshots yet for this game.") {}

size_t SnapshotsPanel::entryCount() const { return ctrl_->snapshots().size(); }

std::vector<RestoreListPanel::RowText> SnapshotsPanel::rows() const {
    std::vector<RowText> out;
    for (const auto& s : ctrl_->snapshots()) {
        char files[48];
        snprintf(files, sizeof(files), "%zu file(s)", s.file_count);
        out.push_back({human_timestamp(s.timestamp), files, human_size(s.total_bytes)});
    }
    return out;
}

std::string SnapshotsPanel::confirmTitle() const { return "Restore this snapshot?"; }

std::string SnapshotsPanel::confirmDetail(size_t i) const {
    const auto items = ctrl_->snapshots();
    if (i >= items.size()) return {};
    return human_timestamp(items[i].timestamp) +
           " \xe2\x80\x94 local safety copy, " + REPLACES_SAVE;
}
