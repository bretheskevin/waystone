# switch/preview — desktop preview of the borealis GUI

Renders the Switch borealis UI (the onboarding wizard screens) on a computer and
screenshots it **headlessly** (Docker + Xvfb + software OpenGL / llvmpipe), so GUI
changes can be verified without deploying a `.nro` to real hardware.

## Usage (from repo root)

```sh
bash switch/preview/build.sh        # build the preview harness (Docker image + borealis + our UI)
bash switch/preview/screenshot.sh   # render headlessly + write PNGs to out/
```

Outputs: `out/{setup-welcome,setup-field,unlock,recovery}.png` (1280×720), rendered in
the shipped **dark theme + Waystone tint**.

## How it works

Compiles the real UI from `switch/source/ui/` (`wizard`, `wizard_activity`,
`setup_activity`, `unlock_activity`, `recovery_key_activity`) against borealis's
GLFW/OpenGL desktop backend, with the Switch-only dependencies **stubbed** in `stubs/`
(swkbd, `vault_helpers`, `saves`, `wsconfig`, the Rust FFI `ws_*`) plus a libnx type
shim in `shims/switch.h`. The UI layer is cleanly separated from libnx business logic,
so only a handful of functions need stubbing.

Two source-clean build workarounds (no patching of tracked source): a `-include cstdint`
force-include and a small `stubs/filestream.c` for a libretro-common symbol borealis's
meson build doesn't include.

Only for iterating on the GUI — the stubs are NOT part of the shipped `.nro` build.
