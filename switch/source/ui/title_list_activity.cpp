#include "title_list_activity.h"
#include "applet_footer_hint.h"
#include "settings_activity.h"
#include "conflicts_activity.h"
#include "conflict_controller.h"
#include "no_internet_activity.h"
#include "net_status.h"
#include "snapshots_controller.h"
#include "snapshots_activity.h"
#include "history_controller.h"
#include "history_activity.h"
#include "borealis_focus.h"
#include <cstdio>

// Muted color for status line (matches wizard footer hints).
static NVGcolor muted_color() { return nvgRGB(0x71, 0x71, 0x7A); }

// Placeholder icon color when a title has no icon JPEG cached.
static NVGcolor placeholder_icon_color() { return nvgRGB(0x38, 0x38, 0x40); }

static const float ICON_SIZE     = 48.0f;
static const float ROW_PADDING   = 5.0f;   // top/bottom only; left/right use ROW_H_PADDING
static const float ROW_H_PADDING = 10.0f;  // left/right padding for row
static const float ROW_GAP       = 12.0f;  // space between icon and name

// NintendoExt PUA button glyphs (same codepoints as wizard_activity.cpp):
//   U+E0A0 "\xEE\x82\xA0" A button
//   U+E0A2 "\xEE\x82\xA2" X button
//   U+E0A3 "\xEE\x82\xA3" Y button
//   U+E0A4 "\xEE\x82\xA4" L shoulder
//   U+E0A5 "\xEE\x82\xA5" R shoulder
static const char* DASHBOARD_HINT =
    "\xEE\x82\xA0 Sync all"
    "   \xc2\xb7   "
    "\xEE\x82\xA2 Conflicts"
    "   \xc2\xb7   "
    "\xEE\x82\xA3 Settings"
    "   \xc2\xb7   "
    "\xEE\x82\xA4 Snapshots"
    "   \xc2\xb7   "
    "\xEE\x82\xA5 History";

TitleListActivity::TitleListActivity(SyncController* ctrl, Session* session)
    : ctrl_(ctrl), session_(session) {}

TitleListActivity::~TitleListActivity() {
    poll_timer_.stop();
    if (ctrl_) { ctrl_->join(); delete ctrl_; }
}

brls::View* TitleListActivity::createContentView() {
    printf("[ui] dashboard createContentView: start\n");

    auto* frame = new brls::AppletFrame();
    frame->setTitle("Waystone");

    // Root column fills the AppletFrame content area.
    // FIX 2: NO horizontal padding on col — ScrollingFrame::onLayout() positions its
    // detached contentView at (scroll->getX(), scroll->getY()), so if scroll is inside
    // a padded col it inherits the col's x offset but NOT the padding as an x shift
    // in the detached layout (detachedOriginX ends up 0). Instead: col has only
    // top/bottom padding; each widget gets its own 32px left margin.
    auto* col = new brls::Box(brls::Axis::COLUMN);
    col->setGrow(1.0f);
    col->setPadding(24.0f, 0.0f, 16.0f, 0.0f);

    // Status line — muted, smaller font; updated by poll timer.
    status_label_ = new brls::Label();
    status_label_->setText("Idle \xc2\xb7 0 games");
    status_label_->setFontSize(18.0f);
    status_label_->setTextColor(muted_color());
    status_label_->setSingleLine(true);
    status_label_->setMarginLeft(32.0f);
    status_label_->setMarginBottom(12.0f);
    col->addView(status_label_);

    // Sync all button — primary style, d-pad focusable.
    auto* sync_btn = new brls::Button();
    sync_btn->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    sync_btn->setText("Sync all");
    sync_btn->setMarginLeft(32.0f);
    sync_btn->setMarginRight(32.0f);
    sync_btn->setMarginBottom(20.0f);
    // White label: set AFTER setStyle (applyStyle() overwrites via style->enabledLabelColor).
    // Button::label is private; access via getChildren()[0] which is the Label from XML.
    if (!sync_btn->getChildren().empty())
        static_cast<brls::Label*>(sync_btn->getChildren()[0])->setTextColor(nvgRGB(255, 255, 255));
    sync_btn->registerClickAction([this](brls::View*) { start_sync_or_gate(); return true; });
    col->addView(sync_btn);

    // Scrollable game list.
    auto* scroll = new brls::ScrollingFrame();
    scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    scroll->setGrow(1.0f);

    title_list_box_ = new brls::Box(brls::Axis::COLUMN);
    title_list_box_->setGrow(1.0f);
    // FIX 2: Horizontal padding 32px on the detached contentView so rows line up with
    // the header widgets (status_label_, sync_btn both have marginLeft=32). The minWidth
    // is set in onContentAvailable() once we know the AppletFrame's laid-out width.
    title_list_box_->setPadding(0.0f, 32.0f, 0.0f, 32.0f);

    const auto& titles = ctrl_->titles();
    for (size_t i = 0; i < titles.size(); i++) {
        const auto& t = titles[i];

        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setPadding(ROW_PADDING, ROW_H_PADDING, ROW_PADDING, ROW_H_PADDING);
        row->setAlignItems(brls::AlignItems::CENTER);

        // Icon slot: fixed-size parent Box + auto Image with FIT, mirroring title_icon.
        // BUG 1 fix: Image with auto dimensions (no setWidth/setHeight on the Image)
        // inside a fixed-size parent already in the tree. setImageFromFile runs AFTER
        // addView, so imageMeasureFunc sees the parent's 48×48 exact constraints instead
        // of UNDEFINED → correct pixel dims → clean NVG paint on deko3d.
        // FIT mode: 256×256 source inside AtMost(48,48) → 48×48 square (1:1 aspect).
        // setWidth/setHeight used on icon_box (they call invalidate); NOT setDimensions.
        auto* icon_box = new brls::Box(brls::Axis::COLUMN);
        icon_box->setWidth(ICON_SIZE);
        icon_box->setHeight(ICON_SIZE);
        icon_box->setGrow(0.0f);
        icon_box->setShrink(0.0f);
        icon_box->setJustifyContent(brls::JustifyContent::CENTER);
        icon_box->setAlignItems(brls::AlignItems::CENTER);
        icon_box->setMarginRight(ROW_GAP);
        row->addView(icon_box);  // parent in tree first

        bool icon_loaded = false;
        if (!t.icon_path.empty()) {
            auto* img = new brls::Image();
            // FIX 1: STRETCH fills the icon_box 48×48 exactly (imageX=0,imageY=0,
            // imageWidth=getWidth(),imageHeight=getHeight()). FIT on a square source
            // was observed to crop the texture horizontally due to NVG paint origin
            // mismatch at non-zero screen x. Square 256×256 icons: STRETCH = no distortion.
            img->setScalingType(brls::ImageScalingType::STRETCH);
            icon_box->addView(img);  // in tree first — correct Yoga constraints
            try {
                img->setImageFromFile(t.icon_path);
                icon_loaded = true;
            } catch (...) {
                // Corrupt/missing cached icon — remove the Image and fall through to placeholder.
                printf("[ui] dashboard: icon load failed for '%s', using placeholder\n", t.name.c_str());
                icon_box->removeView(img);  // img deleted by removeView
            }
        }
        if (!icon_loaded) {
            // No icon or load failed: solid-color placeholder fills the slot.
            auto* placeholder = new brls::Rectangle(placeholder_icon_color());
            placeholder->setWidth(ICON_SIZE);
            placeholder->setHeight(ICON_SIZE);
            icon_box->addView(placeholder);
        }

        // Game name — grows to fill remaining width.
        auto* name_label = new brls::Label();
        name_label->setText(t.name);
        name_label->setFontSize(20.0f);
        name_label->setSingleLine(true);
        name_label->setGrow(1.0f);
        row->addView(name_label);

        title_list_box_->addView(row);
    }

    scroll->setContentView(title_list_box_);
    col->addView(scroll);

    frame->setContentView(col);

    // Clear AppletFrame debug-placeholder footer rectangles and install the hint bar.
    set_footer_hint(frame, DASHBOARD_HINT);

    printf("[ui] dashboard createContentView: done (%zu rows)\n", titles.size());
    return frame;
}

void TitleListActivity::onContentAvailable() {
    printf("[ui] dashboard onContentAvailable\n");
    const size_t game_count = ctrl_->titles().size();

    // FIX 2: The ScrollingFrame detaches its contentView (standalone Yoga layout,
    // YGNodeCalculateLayout with UND×UND). setMaxWidth(scroll->getWidth()) is set,
    // but natural content width < maxWidth → box shrinks to content width (≈959).
    // setWidth = exact screen width forces standalone layout to full screen width so
    // rows stretch to (screen_w - padding_left - padding_right) = screen_w - 64 = 1216,
    // matching the Sync-all button. getContentView() is the AppletFrame (full screen width).
    if (title_list_box_) {
        auto* cv = this->getContentView();
        if (cv) {
            float screen_w = cv->getWidth();
            if (screen_w > 0.0f) {
                printf("[ui] dashboard onContentAvailable: screen_w=%.1f, setWidth\n", screen_w);
                title_list_box_->setWidth(screen_w);
                title_list_box_->invalidate();
            }
        }
    }

    poll_timer_.setCallback([this, game_count]() {
        std::string s = ctrl_->status()
                      + " \xc2\xb7 " + std::to_string(game_count) + " game"
                      + (game_count != 1 ? "s" : "");
        status_label_->setText(s);
    });
    poll_timer_.start(200);

    registerAction("Sync", brls::BUTTON_A, [this](brls::View*) { start_sync_or_gate(); return true; });
    registerAction("Conflicts", brls::BUTTON_X, [this](brls::View*) {
        auto titles = ctrl_->titles();
        auto* cc = new ConflictController(session_->vault, session_->uid, session_->device_id,
                                          session_->dav.as_cfg(), titles);
        cc->start_scan();
        brls::Application::pushActivity(new ConflictsActivity(cc));
        return true;
    });
    registerAction("Settings", brls::BUTTON_Y, [this](brls::View*) {
        brls::Application::pushActivity(new SettingsActivity(session_));
        return true;
    });
    registerAction("Snapshots", brls::BUTTON_LB, [this](brls::View*) {
        auto titles = ctrl_->titles();
        size_t idx = focused_title_index();
        if (idx < titles.size()) {
            auto* sc = new SnapshotsController(titles[idx], session_->uid);
            sc->start_scan();
            brls::Application::pushActivity(new SnapshotsActivity(sc));
        }
        return true;
    });
    registerAction("History", brls::BUTTON_RB, [this](brls::View*) {
        auto titles = ctrl_->titles();
        size_t idx = focused_title_index();
        if (idx < titles.size()) {
            auto* hc = new HistoryController(titles[idx], session_);
            hc->start_scan();
            brls::Application::pushActivity(new HistoryActivity(hc));
        }
        return true;
    });
}

void TitleListActivity::start_sync_or_gate() {
    if (!network_available()) {
        brls::Application::pushActivity(
            new NoInternetActivity(NoInternetReason::NoNetwork, [this]() {
                ctrl_->start();
            }));
        return;
    }
    ctrl_->start();
}

size_t TitleListActivity::focused_title_index() const {
    if (!title_list_box_) return 0;
    return borealis_focused_child_index(title_list_box_);
}
