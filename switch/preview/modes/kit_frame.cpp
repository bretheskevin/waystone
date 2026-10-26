// kit-frame: bare ShellFrame (header, rules, version, guide pill) + a toast.
#include "registry.h"
#include "kit_preview.h"
#include "kit/toast.h"

// The headless capture fires ~5 s after launch; the toast lives 2.5 s, so it is raised late.
static constexpr int TOAST_DELAY_MS = 3500;

static void run(Session&) {
    brls::Application::pushActivity(
        new PreviewShellActivity(TOAST_DELAY_MS, [] { ws_toast("Synced 3 games"); }));
}

static PreviewModeReg reg("kit-frame", &run);
