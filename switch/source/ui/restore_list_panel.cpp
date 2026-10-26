#include "restore_list_panel.h"
#include <cstdio>
#include "keymap_switch.h"
#include "kit/confirm_dialog.h"
#include "kit/toast.h"
#include "kit/ws_views.h"
#include "pane_menu.h"

static constexpr int POLL_MS = 300;

RestoreListPanel::RestoreListPanel(std::string heading, std::string game, std::string empty_text)
    : SlidePanel(WIDTH, false), heading_(std::move(heading)), game_(std::move(game)),
      empty_text_(std::move(empty_text)) {
    setGuide(ws_hint_bar({WsAction::Restore, WsAction::Back}));
}

RestoreListPanel::~RestoreListPanel() {
    stopPolling();
    printf("[ui] %s panel closed (%s)\n", heading_.c_str(), game_.c_str());
}

void RestoreListPanel::stopPolling() {
    *alive_ = false;
    pump_.stop();
    poll_timer_.stop();
}

void RestoreListPanel::buildBody(brls::Box* body) {
    padBody(body);
    auto add_text = [&](const std::string& text, float size, uint32_t color, float top) {
        return addInsetText(body, ws_make_label(text, size, color), top);
    };
    add_text(heading_, WS_DIALOG_TITLE_SIZE, WS_C_TEXT, 0.0f);
    add_text(game_, WS_DIALOG_DETAIL_SIZE, WS_C_MUTED, 4.0f);
    status_label_ = add_text("", WS_ROW_DETAIL_SIZE, WS_C_DETAIL, 12.0f);
    status_label_->setMarginBottom(WS_DIALOG_PAD / 2.0f);
    empty_label_ = add_text(empty_text_, WS_DIALOG_DETAIL_SIZE, WS_C_MUTED, 0.0f);
    empty_label_->setVisibility(brls::Visibility::GONE);

    list_ = ws_add_scroll_menu(body, WIDTH);

    last_entry_count_ = entryCount();
    rebuildList();
}

void RestoreListPanel::onContentAvailable() {
    printf("[ui] %s panel opened (%s, %zu entries)\n", heading_.c_str(), game_.c_str(), last_entry_count_);
    pump_.start();
    poll_timer_.setCallback([this] { poll(); });
    poll_timer_.start(POLL_MS);
    poll();
}

void RestoreListPanel::poll() {
    const BrowsePhase ph = phase();
    const std::string st = status();
    if (st != shown_status_) {
        shown_status_ = st;
        ws_set_label_text(status_label_, st);
        status_label_->setTextColor(ws_nvg(ph == BrowsePhase::Error ? WS_C_ERROR : WS_C_DETAIL));
    }

    if (restoring_ && (ph == BrowsePhase::Done || ph == BrowsePhase::Error)) {
        restoring_ = false;
        if (ph == BrowsePhase::Done) {
            printf("[saves] %s restore done (%s): %s\n", heading_.c_str(), game_.c_str(), st.c_str());
            ws_toast("Restored " + game_);
        } else {
            printf("[saves] %s restore failed (%s): %s\n", heading_.c_str(), game_.c_str(), st.c_str());
            status_label_->setTextColor(ws_nvg(WS_C_ERROR));
        }
    }

    if (ph != BrowsePhase::Ready && ph != BrowsePhase::Done && ph != BrowsePhase::Error) return;
    const size_t count = entryCount();
    if (count == last_entry_count_ || dialog_open_) return;
    printf("[ui] %s panel: %zu -> %zu entries, rebuild scheduled\n", heading_.c_str(),
           last_entry_count_ == SIZE_MAX ? 0 : last_entry_count_, count);
    last_entry_count_ = count;
    pump_.schedule();
}

void RestoreListPanel::refresh() {
    if (dialog_open_ || isClosing()) {
        printf("[ui] %s panel: rebuild skipped (dialog open or closing)\n", heading_.c_str());
        last_entry_count_ = SIZE_MAX;
        return;
    }
    if (!list_->getChildren().empty()) selected_index_ = list_->focusedIndex();
    rebuildList();
    if (!list_->getChildren().empty()) list_->focusIndex(selected_index_);
}

void RestoreListPanel::rebuildList() {
    list_->clear();
    const std::vector<RowText> texts = rows();
    const size_t n         = texts.size();
    const BrowsePhase ph   = phase();
    const bool scanned     = ph != BrowsePhase::Idle && ph != BrowsePhase::Scanning;
    empty_label_->setVisibility(n == 0 && scanned ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    for (size_t i = 0; i < n; i++) {
        const RowText& t = texts[i];
        MenuRow* row     = list_->addTwoLineRow(t.title, t.subtitle, t.detail, {});
        row->registerAction(ws_label(WsAction::Restore), ws_brls(WsAction::Restore),
                            [this, i](brls::View*) {
                                askRestore(i);
                                return true;
                            },
                            false, brls::SOUND_CLICK);
    }
    printf("[ui] %s panel: list built (%zu rows)\n", heading_.c_str(), n);
}

void RestoreListPanel::askRestore(size_t i) {
    if (isClosing() || dialog_open_) return;
    if (restoring_ || phase() != BrowsePhase::Ready || i >= entryCount()) {
        printf("[ui] %s panel: restore %zu ignored (phase=%d, restoring=%d)\n", heading_.c_str(), i,
               (int)phase(), (int)restoring_);
        return;
    }
    printf("[ui] %s panel: confirm restore of entry %zu\n", heading_.c_str(), i);
    dialog_open_ = true;
    OverlayActivity::push(new ConfirmDialog(
        confirmTitle(), confirmDetail(i),
        ws_if_alive(alive_, "restore confirm", [this, i] {
            dialog_open_ = false;
            beginRestore(i);
        }),
        ws_if_alive(alive_, "restore cancel", [this] { dialog_open_ = false; }), WsAction::Restore));
}

void RestoreListPanel::beginRestore(size_t i) {
    printf("[saves] %s restore start (%s, entry %zu)\n", heading_.c_str(), game_.c_str(), i);
    restoring_ = true;
    startRestore(i);
    poll();
}
