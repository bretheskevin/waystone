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

`fonts/User-Switch-Icons.ttf` is generated from real Switch button artwork
(see below). Each glyph = filled button body with the letter knocked out as a
counter, so in the dark-themed footer the letter shows through as background.

| Codepoint | Button  | Shape                        |
|-----------|---------|------------------------------|
| U+E0A0    | A       | filled circle, A knocked out |
| U+E0A1    | B       | filled circle, B knocked out |
| U+E0A2    | X       | filled circle, X knocked out |
| U+E0A3    | Y       | filled circle, Y knocked out |
| U+E0A4    | L       | Switch L shoulder shape      |
| U+E0A5    | R       | Switch R shoulder shape      |
| U+E0A6    | ZL      | Switch ZL trigger shape      |
| U+E0A7    | ZR      | Switch ZR trigger shape      |
| U+E0B5    | Plus +  | filled circle, + knocked out |
| U+E0B6    | Minus − | filled circle, − knocked out |

### Source artwork

Glyphs are derived from the Figma community pack
**"Switch Button Icons (Essential pack)"** by Alvaro Polo Valdenebro:
<https://www.figma.com/community/file/RYQbKWJa1nu4i9NiWbKEI2>

The SVG sources live in `fonts/svg/` (one file per button).
These icons are used **for developer preview only** — they are NOT compiled
into the `.nro` or shipped in any distributed Waystone build. Figma community
files are made available for personal/community use under the terms stated
in the file; use of this artwork is limited to dev tooling in this repository.

### Regenerate

```sh
/opt/homebrew/bin/python3 switch/preview/fonts/gen_from_svg.py
```

Requires: `fonttools` (already installed in Homebrew Python 3.14 on this machine).
The original abstract-shape generator is kept at `fonts/gen_switch_icons.py` for
reference (CC0 1.0). The build script (`build-wizard-inside.sh`) copies the
pre-generated `User-Switch-Icons.ttf` into
`switch/lib/borealis/resources/` automatically at build time.

### Codepoint source
Authoritative codepoints from `WerWolv/libtesla` (MIT-licensed Switch overlay
framework, confirmed on hardware). Note: Plus is U+E0B5, **not** U+E0B8.
