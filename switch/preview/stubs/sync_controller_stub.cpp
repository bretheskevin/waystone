/*
 * Preview stub for SyncController — replaces switch/source/ui/sync_controller.cpp.
 * No-op implementations so the preview links without spinning up sync threads
 * or touching Waystone FFI.
 *
 * titles_ is pre-populated with fixture data for the dashboard preview.
 * icon_path for every fixture points to the bundled placeholder icon so the
 * Image view in TitleListActivity can load a real file in the desktop harness.
 */
#include "sync_controller.h"

bool g_preview_sync_running = false;

// Placeholder icon path — resolved relative to switch/preview/ where the
// preview binary runs, matching BRLS_RESOURCES="./resources/".
static const char* PREVIEW_ICON = "./resources/placeholder_icon.jpg";

static std::vector<TitleInfo> make_fixture_titles() {
    std::vector<TitleInfo> v;
    auto add = [&](uint64_t tid, const char* name) {
        TitleInfo t;
        t.title_id  = tid;
        t.name      = name;
        t.icon_path = PREVIEW_ICON;
        v.push_back(t);
    };
    add(0x0100F2C0115B6000ULL, "The Legend of Zelda: Breath of the Wild");
    add(0x0100A3D008C5C000ULL, "The Legend of Zelda: Tears of the Kingdom");
    add(0x01007EF00011E000ULL, "The Legend of Zelda: Echoes of Wisdom");
    add(0x0100000000010000ULL, "Super Mario Odyssey");
    add(0x010000001B500000ULL, "Pokémon Écarlate");
    add(0x0100ABF008968000ULL, "Metroid Dread");
    add(0x01004D300C5AE000ULL, "Fire Emblem: Three Houses");
    add(0x0100152000022000ULL, "Splatoon 3");
    return v;
}

SyncController::SyncController(WsVault*, AccountUid, std::string,
                               WebDavCfg, std::vector<TitleInfo>, const WaystoneShellConfig*)
    : titles_(make_fixture_titles()), config_(nullptr)
{}

SyncController::~SyncController()
{}

void SyncController::start() {}
void SyncController::join() {}

SyncPhase SyncController::phase() const
{
    return g_preview_sync_running ? SyncPhase::Running : SyncPhase::Idle;
}

std::string SyncController::status() const
{
    return g_preview_sync_running ? "Syncing 3/8" : "Idle";
}

std::vector<TitleResult> SyncController::results() const
{
    std::vector<TitleResult> r(titles_.size());
    if (g_preview_sync_running && r.size() > 2) {
        r[0].state = TitleState::InSync;
        r[1].state = TitleState::InSync;
        r[2].state = TitleState::Active;
    }
    return r;
}

std::string SyncController::step() const { return g_preview_sync_running ? "Uploading" : ""; }
int SyncController::current_index() const { return g_preview_sync_running ? 2 : -1; }
size_t SyncController::bytes_got() const { return g_preview_sync_running ? 512 * 1024 : 0; }
size_t SyncController::bytes_total() const { return g_preview_sync_running ? 2 * 1024 * 1024 : 0; }
float SyncController::progress() const { return g_preview_sync_running ? 0.31f : 0.0f; }
int SyncController::total_count() const { return (int)titles_.size(); }

const std::vector<TitleInfo>& SyncController::titles() const
{
    return titles_;
}
