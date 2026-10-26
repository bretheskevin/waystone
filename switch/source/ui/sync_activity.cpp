#include "sync_activity.h"
#include "keymap_switch.h"
#include "kit/toast.h"
#include "kit/ws_scroll_frame.h"
#include "kit/ws_views.h"
#include "snapshot_browse.h"  // human_size
#include <algorithm>
#include <cstdio>

static constexpr float LIST_MAX_H   = 200.0f;
static constexpr float LIST_ROW_H   = 32.0f;
static constexpr float LIST_STATE_W = 240.0f;

static uint32_t state_color(TitleState s) {
    switch (s) {
    case TitleState::Pending:  return WS_C_MUTED;
    case TitleState::Active:   return WS_C_ACCENT_TEXT;
    case TitleState::Conflict: return WS_C_WARNING;
    case TitleState::Failed:   return WS_C_ERROR;
    default:                   return WS_C_TEXT;
    }
}

SyncActivity::SyncActivity(SyncController* ctrl) : DialogCard(WS_DIALOG_PROGRESS_W), ctrl_(ctrl) {
    printf("[ui] sync modal opened (%zu title(s))\n", ctrl_->titles().size());
    setHint("Syncing\xe2\x80\xa6 please wait");
}

SyncActivity::~SyncActivity() {
    poll_timer_.stop();
    printf("[ui] sync modal closed\n");
}

bool SyncActivity::finished() const {
    SyncPhase ph = ctrl_->phase();
    return ph == SyncPhase::Done || ph == SyncPhase::Error;
}

void SyncActivity::buildBody(brls::Box* body) {
    const auto& titles = ctrl_->titles();
    const float w      = innerWidth();

    title_label_ = addTitle(body, ctrl_->total_count() == 1 && !titles.empty()
                                      ? "Syncing " + titles[0].name
                                      : std::string("Syncing all games"));
    title_label_->setSingleLine(true);
    step_label_ = addDetail(body, "Preparing\xe2\x80\xa6");
    step_label_->setSingleLine(true);

    auto* track = new brls::Box(brls::Axis::ROW);
    track->setWidth(w);
    track->setHeight(WS_BAR_H);
    track->setCornerRadius(WS_BAR_RADIUS);
    track->setBackgroundColor(ws_nvg(WS_C_BAR_TRACK));
    track->setMarginTop(WS_DIALOG_PAD / 2.0f);
    bar_fill_ = new brls::Box(brls::Axis::ROW);
    bar_fill_->setWidth(0.0f);
    bar_fill_->setHeight(WS_BAR_H);
    bar_fill_->setCornerRadius(WS_BAR_RADIUS);
    bar_fill_->setBackgroundColor(ws_nvg(WS_C_BAR));
    track->addView(bar_fill_);
    body->addView(track);

    auto* nums = new brls::Box(brls::Axis::ROW);
    nums->setWidth(w);
    nums->setMarginTop(8.0f);
    bytes_label_ = ws_make_label(" ", WS_ROW_DETAIL_SIZE, WS_C_MUTED);
    bytes_label_->setGrow(1.0f);
    nums->addView(bytes_label_);
    pct_label_ = ws_make_label("0%", WS_ROW_DETAIL_SIZE, WS_C_DETAIL);
    nums->addView(pct_label_);
    body->addView(nums);

    WsScrollFrame* scroll = ws_make_scroll(w);
    scroll->setHeight(std::min(LIST_MAX_H, LIST_ROW_H * (float)titles.size()));
    scroll->setMarginTop(WS_DIALOG_PAD / 2.0f);
    auto* list = new brls::Box(brls::Axis::COLUMN);
    list->setWidth(w);
    for (size_t i = 0; i < titles.size(); i++) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);  // gives the list a focus target for D-pad scrolling
        row->setWidth(w);
        row->setHeight(LIST_ROW_H);
        row->setAlignItems(brls::AlignItems::CENTER);
        brls::Label* name = ws_make_label(titles[i].name, WS_DIALOG_DETAIL_SIZE, WS_C_TEXT);
        name->setWidth(w - LIST_STATE_W);
        row->addView(name);
        brls::Label* state = ws_make_label(title_state_label(TitleState::Pending), WS_ROW_DETAIL_SIZE,
                                           state_color(TitleState::Pending));
        state->setWidth(LIST_STATE_W);
        state->setHorizontalAlign(brls::HorizontalAlign::RIGHT);
        row->addView(state);
        list->addView(row);
        state_labels_.push_back(state);
        shown_states_.push_back(-1);
    }
    scroll->setContentView(list);
    body->addView(scroll);

    root()->registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        close_if_finished("B");
        return true;
    });
}

void SyncActivity::onContentAvailable() {
    poll_timer_.setCallback([this]() { poll(); });
    poll_timer_.start(100);
    poll();
}

void SyncActivity::close_if_finished(const char* why) {
    if (isClosing()) return;
    if (!finished()) {
        printf("[ui] sync modal: %s ignored -- sync still running\n", why);
        return;
    }
    printf("[ui] sync modal dismissed (%s)\n", why);
    std::string headline = ctrl_->status();
    close([headline] { ws_toast(headline); });
}

void SyncActivity::poll() {
    std::vector<TitleResult> results = ctrl_->results();
    for (size_t i = 0; i < results.size() && i < state_labels_.size(); i++) {
        int s = (int)results[i].state;
        if (s == shown_states_[i]) continue;
        shown_states_[i] = s;
        const TitleResult& r = results[i];
        const char* label = (r.state == TitleState::Failed && !r.reason.empty())
                            ? r.reason.c_str() : title_state_label(r.state);
        state_labels_[i]->setText(label);
        state_labels_[i]->setTextColor(ws_nvg(state_color(r.state)));
    }

    const float track_w = innerWidth();
    if (!finished()) {
        int idx = ctrl_->current_index();
        const auto& titles = ctrl_->titles();
        std::string step = ctrl_->step();
        if (idx >= 0 && idx < (int)titles.size() && ctrl_->total_count() != 1)
            step = titles[(size_t)idx].name + (step.empty() ? "" : " \xc2\xb7 " + step);
        step_label_->setText(step.empty() ? std::string("Preparing\xe2\x80\xa6") : step);
        // Empty single-line labels break the borealis layout: hide instead of clearing.
        size_t got = ctrl_->bytes_got(), total = ctrl_->bytes_total();
        ws_show(bytes_label_, got > 0 || total > 0);
        if (got > 0 || total > 0)
            bytes_label_->setText(total > 0 ? human_size(got) + " / " + human_size(total) : human_size(got));
        float p = ctrl_->progress();
        bar_fill_->setWidth(track_w * p);
        pct_label_->setText(std::to_string((int)(p * 100.0f + 0.5f)) + "%");
        return;
    }

    if (finished_logged_) return;
    finished_logged_ = true;
    bool problems = false;
    for (size_t i = 0; i < results.size(); i++)
        if (results[i].state == TitleState::Failed || results[i].state == TitleState::Conflict) problems = true;
    const bool error   = ctrl_->phase() == SyncPhase::Error;
    auto outcome_color = [&](uint32_t ok) { return ws_nvg(error ? WS_C_ERROR : (problems ? WS_C_WARNING : ok)); };
    title_label_->setSingleLine(false);
    title_label_->setText(ctrl_->status());   // headline ("Done — …") or the Error message
    title_label_->setTextColor(outcome_color(WS_C_TEXT));
    step_label_->setVisibility(brls::Visibility::GONE);
    ws_show(bytes_label_, false);
    bar_fill_->setWidth(track_w);
    bar_fill_->setBackgroundColor(outcome_color(WS_C_BAR));
    pct_label_->setText("100%");
    setHint(ws_hint(WsAction::Back, "Close"));
    printf("[ui] sync modal finished: %s\n", ctrl_->status().c_str());
}
