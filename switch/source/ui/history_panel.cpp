#include "history_panel.h"
#include "snapshot_browse.h"  // human_timestamp

HistoryPanel::HistoryPanel(HistoryController* ctrl)
    : ControllerRestorePanel(ctrl, "History", "No history yet for this game.") {}

size_t HistoryPanel::entryCount() const { return ctrl_->entries().size(); }

// Entries are newest-first; the newest is labelled "latest", not "current": a non-fatal history
// PUT failure or an unpulled remote change means it is not necessarily this Switch's save.
std::vector<RestoreListPanel::RowText> HistoryPanel::rows() const {
    std::vector<RowText> out;
    const auto items = ctrl_->entries();
    for (size_t i = 0; i < items.size(); i++)
        out.push_back({human_timestamp(items[i].timestamp), items[i].device_id, i == 0 ? "latest" : ""});
    return out;
}

std::string HistoryPanel::confirmTitle() const { return "Restore this version?"; }

std::string HistoryPanel::confirmDetail(size_t i) const {
    const auto items = ctrl_->entries();
    if (i >= items.size()) return {};
    return human_timestamp(items[i].timestamp) + " \xc2\xb7 " + items[i].device_id +
           " \xe2\x80\x94 " + REPLACES_SAVE;
}
