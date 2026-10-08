#include "sync_activity.h"
#include "applet_footer_hint.h"
#include "keymap_switch.h"
#include "snapshot_browse.h"  // human_size
#include <cstdio>

static NVGcolor muted_color()   { return nvgRGB(0x71, 0x71, 0x7A); }
static NVGcolor sync_color()    { return nvgRGB(0x3B, 0x82, 0xF6); }
static NVGcolor success_color() { return nvgRGB(0x22, 0xC5, 0x5E); }
static NVGcolor warning_color() { return nvgRGB(0xF5, 0x9E, 0x0B); }
static NVGcolor error_color()   { return nvgRGB(0xEF, 0x44, 0x44); }
static NVGcolor track_color()   { return nvgRGB(0x38, 0x38, 0x40); }

static NVGcolor state_color(TitleState s) {
    switch (s) {
    case TitleState::Pending:  return muted_color();
    case TitleState::Active:   return sync_color();
    case TitleState::Conflict: return warning_color();
    case TitleState::Failed:   return error_color();
    default:                   return success_color();
    }
}

SyncActivity::SyncActivity(SyncController* ctrl) : ctrl_(ctrl) {
    printf("[ui] sync modal opened (%zu title(s))\n", ctrl_->titles().size());
}

SyncActivity::~SyncActivity() {
    poll_timer_.stop();
    printf("[ui] sync modal closed\n");
}

bool SyncActivity::finished() const {
    SyncPhase ph = ctrl_->phase();
    return ph == SyncPhase::Done || ph == SyncPhase::Error;
}

brls::View* SyncActivity::createContentView() {
    auto* frame = new brls::AppletFrame();
    frame->setTitle("Sync");

    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1.0f);
    col->setPadding(24.0f, 32.0f, 16.0f, 32.0f);

    title_label_ = new brls::Label();
    title_label_->setFontSize(26.0f);
    title_label_->setSingleLine(true);
    title_label_->setText("Preparing\xe2\x80\xa6");
    col->addView(title_label_);

    step_label_ = new brls::Label();
    step_label_->setFontSize(20.0f);
    step_label_->setTextColor(sync_color());
    step_label_->setMarginTop(8.0f);
    col->addView(step_label_);

    bytes_label_ = new brls::Label();
    bytes_label_->setFontSize(18.0f);
    bytes_label_->setTextColor(muted_color());
    col->addView(bytes_label_);

    bar_track_ = new brls::Box(brls::Axis::ROW);
    bar_track_->setHeight(8.0f);
    bar_track_->setMarginTop(16.0f);
    bar_fill_ = new brls::Rectangle(sync_color());
    bar_fill_->setWidth(0.0f);
    bar_fill_->setHeight(8.0f);
    bar_track_->addView(bar_fill_);
    auto* rest = new brls::Rectangle(track_color());
    rest->setHeight(8.0f);
    rest->setGrow(1.0f);
    bar_track_->addView(rest);
    col->addView(bar_track_);

    pct_label_ = new brls::Label();
    pct_label_->setFontSize(18.0f);
    pct_label_->setTextColor(muted_color());
    pct_label_->setMarginBottom(12.0f);
    col->addView(pct_label_);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    scroll->setGrow(1.0f);
    auto* list = new brls::Box(brls::Axis::COLUMN);
    const auto& titles = ctrl_->titles();
    for (size_t i = 0; i < titles.size(); i++) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);  // gives the list a focus target for D-pad scrolling
        row->setPadding(6.0f, 10.0f, 6.0f, 10.0f);
        auto* name = new brls::Label();
        name->setText(titles[i].name);
        name->setFontSize(20.0f);
        name->setSingleLine(true);
        name->setGrow(1.0f);
        row->addView(name);
        auto* state = new brls::Label();
        state->setFontSize(18.0f);
        state->setSingleLine(true);
        state->setText(title_state_label(TitleState::Pending));
        state->setTextColor(state_color(TitleState::Pending));
        row->addView(state);
        list->addView(row);
        state_labels_.push_back(state);
        shown_states_.push_back(-1);
    }
    scroll->setContentView(list);
    col->addView(scroll);

    frame->setContentView(col);
    footer_ = set_footer_hint(frame, "Syncing\xe2\x80\xa6 please wait");
    return frame;
}

void SyncActivity::onContentAvailable() {
    poll_timer_.setCallback([this]() { poll(); });
    poll_timer_.start(100);
    registerAction(ws_label(WsAction::Back), ws_brls(WsAction::Back), [this](brls::View*) {
        close_if_finished("B");
        return true;
    });
    registerAction(ws_label(WsAction::Continue), ws_brls(WsAction::Continue), [this](brls::View*) {
        close_if_finished("A");
        return true;
    });
    poll();
}

void SyncActivity::close_if_finished(const char* why) {
    if (!finished()) {
        printf("[ui] sync modal: %s ignored -- sync still running\n", why);
        return;
    }
    printf("[ui] sync modal dismissed (%s)\n", why);
    poll_timer_.stop();
    brls::Application::popActivity();
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
        state_labels_[i]->setTextColor(state_color(r.state));
    }

    const float track_w = bar_track_ ? bar_track_->getWidth() : 0.0f;
    if (!finished()) {
        int idx = ctrl_->current_index();
        const auto& titles = ctrl_->titles();
        title_label_->setText(idx >= 0 && idx < (int)titles.size() ? titles[(size_t)idx].name
                                                                  : std::string("Preparing\xe2\x80\xa6"));
        step_label_->setText(ctrl_->step());
        size_t got = ctrl_->bytes_got(), total = ctrl_->bytes_total();
        if (got > 0 || total > 0)
            bytes_label_->setText(total > 0 ? human_size(got) + " / " + human_size(total) : human_size(got));
        else
            bytes_label_->setText("");
        float p = ctrl_->progress();
        if (track_w > 0.0f) bar_fill_->setWidth(track_w * p);
        pct_label_->setText(std::to_string((int)(p * 100.0f + 0.5f)) + "%");
        return;
    }

    if (finished_logged_) return;
    finished_logged_ = true;
    bool problems = false;
    for (size_t i = 0; i < results.size(); i++)
        if (results[i].state == TitleState::Failed || results[i].state == TitleState::Conflict) problems = true;
    NVGcolor c = ctrl_->phase() == SyncPhase::Error ? error_color()
               : (problems ? warning_color() : success_color());
    title_label_->setText(ctrl_->status());   // headline ("Done — …") or the Error message
    title_label_->setTextColor(c);
    step_label_->setText("");
    bytes_label_->setText("");
    if (track_w > 0.0f) bar_fill_->setWidth(track_w);
    bar_fill_->setColor(c);
    pct_label_->setText("100%");
    if (footer_) footer_->setText(ws_hint(WsAction::Continue) + "  " + ws_hint(WsAction::Back));
    printf("[ui] sync modal finished: %s\n", ctrl_->status().c_str());
}
