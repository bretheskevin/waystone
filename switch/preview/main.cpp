/*
 * Waystone preview harness — renders Switch UI screens on desktop.
 *
 * Usage: waystone_preview <mode>
 * Modes self-register from switch/preview/modes/*.cpp (see modes/registry.h);
 * an unknown mode prints the registered names.
 *
 * Set BOREALIS_THEME=DARK before running to force the dark theme.
 * The binary must be launched from switch/preview/ so that ./resources/ is found.
 */

#include <borealis.hpp>
#include "theme_tint.h"
#include "worker_reaper.h"
#include "session.h"
#include "modes/registry.h"
#include <cstdio>

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "setup-welcome";

    PreviewModeFn run = preview_find(mode);
    if (!run) {
        fprintf(stderr, "[ui] unknown preview mode '%s'; registered modes:\n", mode);
        for (const auto& name : preview_mode_names())
            fprintf(stderr, "  %s\n", name.c_str());
        return 1;
    }

    apply_waystone_tint();

    brls::Logger::setLogLevel(brls::LogLevel::WARNING);

    if (!brls::Application::init()) {
        brls::Logger::error("Failed to initialise borealis");
        return 1;
    }

    brls::Application::createWindow("Waystone Preview");
    worker_reaper_start();

    static Session session;

    printf("[ui] preview mode '%s' starting\n", mode);
    run(session);

    while (brls::Application::mainLoop())
        ;

    return 0;
}
