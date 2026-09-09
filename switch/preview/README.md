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

## Controller-glyph font (`fonts/`)

The borealis GLFW backend (desktop builds) looks for
`resources/User-Switch-Icons.ttf` in the run directory to populate the
`FONT_SWITCH_ICONS` slot — the fallback registered **before** Material Icons.

`fonts/User-Switch-Icons.ttf` is a generated preview font covering the ten
NintendoExt codepoints used by the wizard footer:

| Codepoint | Button  | Glyph shape  |
|-----------|---------|--------------|
| U+E0A0    | A       | circle ring  |
| U+E0A1    | B       | circle ring  |
| U+E0A2    | X       | circle ring  |
| U+E0A3    | Y       | circle ring  |
| U+E0A4    | L       | rectangle    |
| U+E0A5    | R       | rectangle    |
| U+E0A6    | ZL      | rectangle    |
| U+E0A7    | ZR      | rectangle    |
| U+E0B5    | Plus +  | + crosshair  |
| U+E0B6    | Minus − | − bar        |

**License:** CC0 1.0 Universal (no rights reserved).  
This font is NOT Nintendo's proprietary font. It is a purpose-built preview stub.

**Regenerate with:**
```sh
python3 switch/preview/fonts/gen_switch_icons.py
```
The build script (`build-wizard-inside.sh`) copies the font into
`switch/lib/borealis/resources/` automatically at build time.

### Codepoint source
Authoritative codepoints from `WerWolv/libtesla` (MIT-licensed Switch overlay
framework, confirmed on hardware). Note: Plus is U+E0B5, **not** U+E0B8.
