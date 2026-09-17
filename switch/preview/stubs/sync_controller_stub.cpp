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
                               WebDavCfg, std::vector<TitleInfo>)
    : titles_(make_fixture_titles())
{}

SyncController::~SyncController()
{}

void SyncController::start() {}
void SyncController::join() {}

SyncPhase SyncController::phase() const
{
    return SyncPhase::Idle;
}

std::string SyncController::status() const
{
    return "Idle";
}

int SyncController::pushed_count() const { return 0; }
int SyncController::restored_count() const { return 0; }

const std::vector<TitleInfo>& SyncController::titles() const
{
    return titles_;
}
