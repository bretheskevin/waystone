#include "restore_browse_screen.h"
#include "app.h"
#include "widgets.h"
#include "theme.h"
#include <cstdio>

RestoreBrowseScreen::RestoreBrowseScreen()
    : confirm_restore_(false)
{}

RestoreBrowseScreen::~RestoreBrowseScreen() {}

float RestoreBrowseScreen::status_area_height() const {
    if (!confirm_restore_ && !status_text_.empty()) return 18.0f;
    return 0.0f;
}

void RestoreBrowseScreen::draw_top_status(C2D_TextBuf buf, float sy) {
    if (confirm_restore_ || status_text_.empty()) return;
    u32 clr = CLR_NEUTRAL_400;
    BrowsePhase ph = browse_phase();
    if (ph == BrowsePhase::Error) clr = CLR_ERROR;
    else if (ph == BrowsePhase::Done) clr = CLR_SUCCESS;
    else if (ph == BrowsePhase::Scanning ||
             ph == BrowsePhase::Restoring) clr = CLR_SYNC;
    draw_text_centered(buf, 0, sy, 0.5f, TEXT_SM, clr,
                       status_text_.c_str(), (float)SCREEN_TOP_W);
}

void RestoreBrowseScreen::draw_detail(C2D_TextBuf buf,
                                      float area_y, float area_h) {
    if (confirm_restore_) {
        draw_confirm_banner(buf, area_y, area_h,
                            confirm_line1(),
                            "Current save will be backed up first.",
                            ws_confirm_cancel_hint().c_str(),
                            54.0f);
        return;
    }
    draw_selected_detail(buf, area_y, area_h);
}

std::vector<Action> RestoreBrowseScreen::actions() {
    std::vector<Action> a;
    if (confirm_restore_) {
        a.push_back(make_action(WsAction::Confirm, ACT_CONFIRM, true, ButtonStyle::PRIMARY));
    } else {
        bool can = (browse_phase() == BrowsePhase::Ready && item_count() > 0);
        a.push_back(make_action(WsAction::Restore, ACT_RESTORE, can, ButtonStyle::PRIMARY));
    }
    return a;
}

void RestoreBrowseScreen::on_action(int id) {
    switch (id) {
    case ACT_RESTORE:
        printf("[%s] entering confirm for idx=%zu\n", log_tag(), cursor_);
        confirm_restore_ = true;
        break;
    case ACT_CONFIRM:
        printf("[%s] confirmed restore idx=%zu\n", log_tag(), cursor_);
        browse_start_restore(cursor_);
        confirm_restore_ = false;
        break;
    }
}

void RestoreBrowseScreen::on_back() {
    if (confirm_restore_) {
        printf("[%s] confirm cancelled\n", log_tag());
        confirm_restore_ = false;
        return;
    }
    printf("[%s] popping screen\n", log_tag());
    App::instance().pop_screen();
}

bool RestoreBrowseScreen::modal_active() const {
    return confirm_restore_;
}
