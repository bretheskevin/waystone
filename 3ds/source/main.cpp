#include <cstdio>
#include <3ds.h>

struct Vault;
extern "C" {
#include "waystone.h"
}

int main(int argc, char* argv[]) {
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("=== Waystone 3DS Engine (stub) ===\n\n");
    printf("Toolchain smoke test OK.\n");
    printf("FFI header included successfully.\n");

    printf("\nPress START to exit.\n");
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) break;
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
