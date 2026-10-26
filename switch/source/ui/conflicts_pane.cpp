#include "conflicts_pane.h"
#include "borealis_focus.h"
#include "keymap_switch.h"
#include "pane_menu.h"
#include "snapshot_browse.h"
#include "worker_reaper.h"
#include "kit/confirm_dialog.h"
#include "kit/overlay.h"
#include "kit/ws_views.h"
#include <cstdio>

static constexpr float STATUS_MARGIN_TOP    = 16.0f;
static constexpr float STATUS_MARGIN_BOTTOM = 8.0f;
static constexpr int   POLL_MS              = 300;

static std::string mtime_or_unknown(const std::string& mtime) {
    return mtime.empty() ? "time unknown" : human_timestamp(mtime);
}

static std::string conflict_subtitle(const ConflictView& item) {
    return "Local " + mtime_or_unknown(item.local_mtime) + " \xc2\xb7 Remote " +
           mtime_or_unknown(item.remote_mtime) + ", " + item.remote_device_id;
}

ConflictsPane::ConflictsPane(Session* session, std::vector<TitleInfo> titles)
    : brls::Box(brls::Axis::COLUMN), session_(session), titles_(std::move(titles)) {
    ws_setup_pane(this);

    status_label_ = ws_make_label("", WS_STATUS_SIZE, WS_C_MUTED);
    status_label_->setMarginLeft(WS_PANE_NAME_INSET);
    status_label_->setMarginTop(STATUS_MARGIN_TOP);
    status_label_->setMarginBottom(STATUS_MARGIN_BOTTOM);
    addView(status_label_);

    list_ = ws_add_pane_menu(this, &scroll_);

    empty_ = new brls::Box(brls::Axis::COLUMN);
    empty_->setGrow(1.0f);
    empty_->setJustifyContent(brls::JustifyContent::CENTER);
    empty_->setAlignItems(brls::AlignItems::CENTER);
    empty_->addView(ws_make_label("No conflicts", WS_STATUS_SIZE, WS_C_MUTED));
    empty_->setVisibility(brls::Visibility::GONE);
    addView(empty_);

    pump_.start();
    poll_timer_.setCallback([this]() {
        if (!ctrl_) return;
        ws_set_label_text(status_label_, ctrl_->status());
        if (ctrl_->version() != seen_version_ && !dialog_open_) pump_.schedule();
    });
    poll_timer_.start(POLL_MS);
    printf("[ui] conflicts pane: created (%zu titles)\n", titles_.size());
}

ConflictsPane::~ConflictsPane() {
    pump_.stop();
    poll_timer_.stop();
    reap_worker(ctrl_);
    printf("[ui] conflicts pane: destroyed\n");
}

std::string ConflictsPane::guide() const {
    return ws_hint_bar({WsAction::KeepLocal, WsAction::KeepRemote, WsAction::Back, WsAction::Quit});
}

void ConflictsPane::onShown() {
    if (ctrl_ && ctrl_->is_running()) {
        printf("[ui] conflicts pane: shown, scan still running -- keeping it\n");
        return;
    }
    if (dialog_open_) {
        printf("[ui] conflicts pane: shown with a confirm open -- no rescan\n");
        return;
    }
    reap_worker(ctrl_);
    ctrl_ = new ConflictController(session_->vault, session_->uid, session_->device_id,
                                   session_->dav.as_cfg(), titles_, session_->config);
    seen_version_ = UINT32_MAX;
    selected_id_  = 0;
    printf("[conflict] ui: scan started (%zu titles)\n", titles_.size());
    ctrl_->start_scan();
    pump_.schedule();
}

void ConflictsPane::refresh() {
    if (!ctrl_) return;
    if (dialog_open_) {
        printf("[ui] conflicts pane: refresh skipped (confirm open)\n");
        return;
    }
    const bool focused = borealis_focus_within(this);
    // Capture focus by stable id BEFORE rebuilding (old rows still exist).
    if (focused && !items_.empty()) {
        size_t fi = list_->focusedIndex();
        if (fi < items_.size()) { selected_id_ = items_[fi].id; focus_index_ = fi; }
    }
    // Read the version BEFORE copying so a concurrent mutation re-triggers a refresh.
    seen_version_ = ctrl_->version();
    items_ = ctrl_->views();
    ConflictPhase phase = ctrl_->phase();
    if (phase == ConflictPhase::Ready || phase == ConflictPhase::Resolving ||
        phase == ConflictPhase::Done)
        known_ = static_cast<int>(items_.size());
    ws_set_label_text(status_label_, ctrl_->status());

    rebuild_list();

    const bool scanning = phase == ConflictPhase::Idle || phase == ConflictPhase::Scanning;
    const bool empty = items_.empty();
    scroll_->setVisibility(empty ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    empty_->setVisibility(empty && !scanning ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    // Holds focus when the list is empty so the dashboard's Back still reaches the pane.
    empty_->setFocusable(empty && !scanning);

    if (focused) focus_selected_row();
    printf("[ui] conflicts pane: refresh v=%u items=%zu known=%d focused=%d\n", seen_version_,
           items_.size(), known_, focused ? 1 : 0);
}

void ConflictsPane::rebuild_list() {
    list_->clear();
    for (const ConflictView& item : items_) {
        MenuRow* row = list_->addTwoLineRow(item.title_name, conflict_subtitle(item),
                                            item.queued ? "Queued" : "", {});
        row->registerAction(ws_label(WsAction::KeepLocal), ws_brls(WsAction::KeepLocal),
                            [this, item](brls::View*) {
                                if (!item.queued) confirm_resolve(item, true);
                                return true;
                            },
                            false, brls::SOUND_CLICK);
        row->registerAction(ws_label(WsAction::KeepRemote), ws_brls(WsAction::KeepRemote),
                            [this, item](brls::View*) {
                                if (!item.queued) confirm_resolve(item, false);
                                return true;
                            },
                            false, brls::SOUND_CLICK);
    }
}

void ConflictsPane::focus_selected_row() {
    if (items_.empty()) {
        if (empty_->isFocusable()) brls::Application::giveFocus(empty_);
        return;
    }
    size_t idx = focus_index_;
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i].id == selected_id_) { idx = i; break; }
    if (idx >= items_.size()) idx = items_.size() - 1;
    list_->focusIndex(idx);
    focus_index_ = idx;
}

void ConflictsPane::confirm_resolve(const ConflictView& item, bool keep_local) {
    // A reaped History/Snapshots restore may still be writing this save.
    if (refuse_while_reaping("[conflict] ui:", keep_local ? "keep local" : "keep remote")) return;
    const uint32_t id = item.id;
    selected_id_ = id;
    dialog_open_ = true;
    printf("[conflict] ui: keep %s requested id=%u (confirming)\n", keep_local ? "local" : "remote", id);

    ConflictController* ctrl = ctrl_;
    auto on_confirm = ws_if_alive(alive_, "conflict resolve", [this, ctrl, id, keep_local] {
        dialog_open_ = false;
        if (ctrl_ != ctrl) {
            printf("[conflict] ui: controller replaced, resolve id=%u dropped\n", id);
        } else if (keep_local) {
            printf("[conflict] ui: keep local id=%u\n", id);
            ctrl_->resolve_keep_local(id);
        } else {
            printf("[conflict] ui: keep remote confirmed id=%u\n", id);
            ctrl_->resolve_keep_remote(id);
        }
        pump_.schedule();
    });
    auto on_cancel = ws_if_alive(alive_, "conflict cancel", [this, id] {
        dialog_open_ = false;
        printf("[conflict] ui: resolve id=%u cancelled\n", id);
        pump_.schedule();
    });

    if (keep_local)
        OverlayActivity::push(new ConfirmDialog(
            "Keep local save?", "Overwrites the server copy of " + item.title_name + ".",
            on_confirm, on_cancel, WsAction::KeepLocal));
    else
        OverlayActivity::push(new ConfirmDialog(
            "Keep server save?", "Replaces the save on this Switch with the server copy.",
            on_confirm, on_cancel, WsAction::KeepRemote));
}
