# Switch homebrew (`switch/`) — development guide

The Nintendo Switch shell: a **borealis** (deko3d) GUI `.nro` that statically links the Rust
`libwaystone_ffi.a`, `switch-curl`, and `libnx`, built with **devkitPro / devkitA64**.

**Develop against the authoritative docs — do not guess at Switch/NRO behaviour.**

## Reference docs (the authorities)

| Topic | Where |
|-------|-------|
| libnx (C runtime; NRO loader, syscalls, services) | https://github.com/switchbrew/libnx — source is the truth; e.g. the NRO loader is `nx/source/runtime/dynamic.c`. Doxygen: https://switchbrew.github.io/libnx/ |
| Switch OS / NRO / hardware internals | https://switchbrew.org/wiki |
| devkitPro toolchain (devkitA64, portlibs, `switch_rules`) | https://devkitpro.org , https://github.com/devkitPro |
| deko3d (GPU) | https://github.com/devkitPro/deko3d |
| borealis (UI lib, vendored in `lib/borealis`) | https://github.com/natinusala/borealis — but this repo vendors an OLD/minimal commit; **read the vendored source**, not upstream docs (many widgets/APIs are absent). |

## HARD CONSTRAINT — NRO relocations (crashes at boot if violated)

libnx's NRO loader `__nx_dynamic` / `_dynProcessRela` (`nx/source/runtime/dynamic.c`) applies
**only `R_AARCH64_NONE` + `R_AARCH64_RELATIVE`** (plus `DT_RELR`). Any other relocation type hits
`default: diagAbortWithResult(LibnxError_BadReloc)` → **fatal abort at startup**. So a `.nro` must
carry **only relative relocations**.

- Statically linking the **Rust** FFI lib whose exports have default visibility makes C++→Rust
  calls go through the PLT (`R_AARCH64_JUMP_SLOT`); libnx does **not** apply `.rela.plt`, so the
  first FFI call jumps to garbage (Instruction Abort at runtime).
- "Fixing" that with `-Wl,-Bsymbolic` / `-fno-plt` produces `R_AARCH64_GLOB_DAT`, which libnx
  **aborts on at boot**. Both are wrong.
- CORRECT: make static-lib FFI symbols bind **locally** (`-Wl,--exclude-libs,ALL`, or hidden
  visibility) → direct `bl` / `RELATIVE` GOT entries.

**Verify every build** (matches libnx's supported set — no hardware needed):
```sh
aarch64-none-elf-readelf -r switch/waystone.elf \
  | grep -oE 'R_AARCH64_[A-Z0-9_]+' | sort | uniq -c
# MUST be R_AARCH64_RELATIVE / R_AARCH64_NONE only. Any GLOB_DAT/JUMP_SLOT = will crash on boot.
```

## HARD GOTCHA — borealis focused-view use-after-free

In the pinned borealis, `Application::handleAction` iterates the **focused view's** action list and
calls `playClickAnimation()`/`getParent()` on it **after** the listener returns. So deleting or
rebuilding the currently-focused view from inside its own action callback = use-after-free (Data
Abort on hardware). The wizard defers all rebuilds one frame via `WizardActivity::RefreshPump`
(`brls::RepeatingTask`) + `schedule_refresh()`, which runs `refresh()` in `Application::frame()`
after input dispatch unwinds. Two safe patterns: (1) mutate views in place (`setText`), or
(2) defer the rebuild. Never rebuild the focused view synchronously inside its action.

## Dev workflow

- **Preview on desktop before flashing.** `switch/preview/` compiles the real `source/ui/*.cpp`
  against borealis headlessly (Docker + Xvfb + GLFW/llvmpipe). `bash switch/preview/build.sh` is a
  fast compile check; `screenshot.sh` / the `transition` mode capture visuals. Get UI/animation
  design signed off on desktop before building an `.nro` (the hardware loop is slow).
- **Builds** (devkitPro Docker image `waystone-switch`):
  `docker run --rm -v "$PWD":/work -w /work/switch waystone-switch bash -lc "make"` (GUI) and
  `… make CONSOLE=1` (console variant). Incremental `make` (no `clean`) only recompiles changed
  files — skip `make clean` while iterating. The Makefile applies the borealis theme-tint patch
  and assembles romfs from the vendored borealis tree at build time.
- On-hardware: launch in **full application mode** (hold **R** over a game) for save-mount + enough
  heap; Atmosphère dumps crash logs to `sdmc:/atmosphere/crash_reports/*.log` — symbolize with
  `aarch64-none-elf-addr2line -f -C -i -e switch/waystone.elf <module-offset…>`.
