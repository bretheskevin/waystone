# 3DS homebrew (`3ds/`) — development guide

The Nintendo 3DS shell: a **citro2d/citro3d** GUI `.3dsx` that statically links the Rust
`libwaystone_ffi.a`, `3ds-curl` (+ `3ds-mbedtls`), and `libctru`, built with **devkitPro / devkitARM**.
Mirrors the Switch shell (`switch/`) but on a dual-screen, touch, per-title-save device with **no
account/uid**.

**Develop against the authoritative docs — do not guess at 3DS/libctru/citro2d behaviour.**
Guessing an API that was renamed/removed produces an **uncompilable build** (this class of bug bit the
Switch shell: libnx's `swkbdConfigSetStringLenMaxExt` had been removed — verify every call against the
pinned library's docs, not muscle memory).

## Reference docs (the authorities)

| Topic | Where |
|-------|-------|
| devkitPro toolchain (devkitARM, portlibs, `3ds_rules`) | https://devkitpro.org , https://devkitpro.org/wiki/Getting_Started . `3ds_rules` ships **inside devkitARM** at `$DEVKITARM/3ds_rules` (no hosted page) — the `3ds-examples` Makefiles are the de-facto reference. Org: https://github.com/devkitPro |
| libctru (3DS C runtime: services, applets, FS, HID, threads, GPU init) | Repo (source is the truth): https://github.com/devkitPro/libctru . Doxygen: https://libctru.devkitpro.org/ (live + Google-indexed; returns 403 to bots, opens fine in a browser). |
| citro2d (2D drawing on the PICA200) | Repo: https://github.com/devkitPro/citro2d . Doxygen: https://citro2d.devkitpro.org/ (e.g. init/teardown in `base_8h.html`). |
| citro3d (low-level PICA200 GPU wrapper) | Repo: https://github.com/devkitPro/citro3d . **No hosted Doxygen exists** — read the repo's `include/` headers, or generate docs locally from its `Doxyfile`. |
| Official examples (citro2d init, sprites, threads, swkbd) | https://github.com/devkitPro/3ds-examples . citro2d examples live under **`graphics/gpu/`** (`2d_shapes/`, `gpusprites/`) — there is no `graphics/citro2d/` folder. citro2d init reference: `graphics/gpu/2d_shapes/source/main.c`. |
| 3DS OS / hardware / services / file formats | https://www.3dbrew.org/wiki/Main_Page (the 3DS equivalent of switchbrew.org). |
| swkbd (software keyboard applet) | Header `libctru/include/3ds/applets/swkbd.h`. Doxygen: https://libctru.devkitpro.org/swkbd_8h_source.html . Official example: https://libctru.devkitpro.org/input_2software-keyboard_2source_2main_8c-example.html |
| Checkpoint (reference citro2d save manager — **read for patterns, do NOT copy code**) | https://github.com/BernardoGiordano/Checkpoint — **GPLv3**. We reference its *behaviour* clean-room (restore logic); copying its source into this repo would impose GPLv3. Keep it reference-only. |

## HARD CONSTRAINT — citro2d/citro3d link order + init (blank screen / link errors if violated)

- **`LIBS` link order** (GNU ld is order-sensitive): `-lcitro2d -lcitro3d -lctru -lm` — citro2d depends
  on citro3d depends on libctru. Wrong order → unresolved-symbol link failure. Keep `-lwaystone_ffi`
  and the curl/mbedtls libs ahead of `-lctru` as they already are.
- **Init sequence** (from the official `2d_shapes` example + citro2d `base.h`):
  ```c
  gfxInitDefault();
  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();
  C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);   // 400x240, NO touch
  C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);   // 320x240, touchscreen
  ```
  Per-frame: `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` → for each target `C2D_TargetClear(t, bg)` +
  `C2D_SceneBegin(t)` + draw → `C3D_FrameEnd(0)`. Teardown (reverse): `C2D_Fini(); C3D_Fini(); gfxExit();`.
- System font is available via citro2d (`C2D_Text*` uses the shared system font) — **no font asset to
  ship**. romfs stays `cacert.pem`-only. Do NOT `consoleInit()` a screen you also drive with citro2d.

## HARD CONSTRAINT — build stays `-fno-exceptions -fno-rtti -std=c++11`

The 3DS build sets these **globally** in the Makefile (unlike the Switch GUI, which had to relax them
for borealis). citro2d/citro3d are C libraries, so keep them global. Consequences:
- **No `throw`/`try`/`catch`/`dynamic_cast`, no nlohmann, no RTTI.** The engine already avoids these.
- **No `std::thread`** for the sync/KDF worker — it can pull in exception machinery and its failure
  path throws. Use libctru **`threadCreate`** (`threadCreate`/`threadJoin`/`threadFree`,
  `svcGetThreadPriority(&prio, CUR_THREAD_HANDLE)` then create at `prio-1`, adequate stack for
  curl+FFI). This is the native, exception-free path. (The Switch's `SyncController` uses `std::thread`;
  the 3DS `sync_worker` mirrors its *surface* but uses `threadCreate`.)

## HARD GOTCHA — one `int main()` only (duplicate-symbol link error)

The GUI entry (`app_main.cpp`) and the console driver (`main.cpp`) both define `int main()`. Exactly one
compiles. The Makefile `CONSOLE` guard must `filter-out` the inactive entry point in **both** branches:
default (GUI) drops `main.cpp` + drops nothing from `source/ui`; `CONSOLE=1` drops `app_main.cpp` **and**
all of `source/ui/*` **and** the citro2d libs. Mirror `switch/Makefile`'s `CONSOLE` handling. The
Makefile globs `$(dir)/*.cpp` **non-recursively**, so `source/ui` must be added to `SOURCES` + `INCLUDES`.

## PERMANENT — CIA Title ID `0x000400000FF3FF00`

The `.cia`'s title ID (`UniqueId 0xFF3FF`, homebrew range) is baked into `3ds/app.rsf`
(`TitleInfo/UniqueId`). **NEVER change it** once any user has installed the CIA — the 3DS treats a
different title ID as a different application, so a change gives users a duplicate Home-Menu entry
on update instead of an in-place upgrade.

## swkbd — differs from libnx (verify against libctru docs)

libctru's software keyboard is **not** the libnx API. Use: `swkbdInit(&sw, SWKBD_TYPE_NORMAL, numButtons,
maxTextLen)`, `swkbdSetHintText`, secrets → `swkbdSetPasswordMode(&sw, SWKBD_PASSWORD_HIDE)`,
`swkbdSetInitialText`, then `swkbdInputText(&sw, buf, sizeof(buf))` (blocking; takes over both screens —
re-enter the C2D loop afterward). Zeroize the local `char buf[]` with `shell-common/secure_clear.h` for
secret fields. Confirm each setter still exists at the pinned libctru version via
https://libctru.devkitpro.org/swkbd_8h_source.html before using it.

## Engine facts (unchanged by the GUI)

- **No `AccountUid`** — 3DS saves are per-title. Sync runs through the shared engine
  (`shell-common/sync_engine.h`) with `ctr_shell_ops()` (see `source/sync.h`), whose ctx is unused; the
  `Session`/`sync_worker` drop the uid the Switch carries.
- Bootstrap order (LIFO cleanup): `gfxInitDefault`→`psInit`→`socInit(aligned SOC buffer)`→`romfsInit`
  (before curl)→`curl_global_init`; teardown reverse. GUI adds `C3D_Init`/`C2D_Init` after
  `gfxInitDefault` and `C2D_Fini`/`C3D_Fini` before `gfxExit`.
- **Argon2 KDF (`ws_vault_init`/unlock) + network sync are heavy/blocking** → run on the `threadCreate`
  worker, never the render thread, so `aptMainLoop` keeps pumping and the home menu never flags the app
  unresponsive (the Switch learned this — Create-Vault black-screen, commit 617565c).
- Every FFI `WsBuf`→`ws_buf_free`, `WsVault*`→`ws_vault_free`, once (watch error/skip paths).
  `struct Vault;` before including the cbindgen `waystone.h`.
- See also the 3DS engine gotchas in Serena memory `3ds` (getrandom shim, `jsmn.h` `JSMN_HEADER`
  duplicate-symbol rule, FS write truncation, restore-correctness vs Checkpoint).

## Dev workflow

- **Build/verify is COMPILE+LINK ONLY on the host** — no hardware/emulator here; the citro2d UI renders
  only on a real 3DS (the user runs it on-device). Never claim visual correctness from a host build.
- **Builds** (devkitARM Docker image `waystone-3ds`, from `3ds/Dockerfile`; the base ships
  `3ds-curl`/`3ds-mbedtls`/`libctru`/`3ds_rules` — the Dockerfile does **no** `dkp-pacman`, only injects
  the corp CA from gitignored `3ds/certs/*.pem` + rust nightly):
  - GUI (default) — **arm64-native, fast loop**: `docker build -t waystone-3ds -f 3ds/Dockerfile 3ds/`
    then `docker run --rm -v "$PWD":/work -w /work waystone-3ds bash -lc "cargo +nightly build
    -Zbuild-std=core,alloc --target armv6k-nintendo-3ds --features 3ds -p waystone-ffi --release &&
    make -C 3ds clean && make -C 3ds"` → `3ds/waystone-3ds-spike.3dsx`.
  - Console fallback: `make -C 3ds clean && make -C 3ds CONSOLE=1` (no citro2d) — the verified engine driver.
  - CIA (installable) — **use `bash 3ds/build-cia.sh`**. This script builds an **amd64** container
    (via Rosetta on Apple Silicon) because `makerom` and `bannertool` are x86_64-only prebuilts; they
    cannot execute during an arm64 Docker build and cannot run in an arm64 container at runtime.
    The normal arm64 image is unchanged and stays fast. The script:
    1. `docker build --platform linux/amd64 -t waystone-3ds-amd64 -f 3ds/Dockerfile 3ds/`
    2. `docker run --platform linux/amd64 ...` → runs FFI build + `make -C 3ds cia`
    → `3ds/waystone-3ds-spike.cia`.
    **Do NOT** run `make -C 3ds cia` inside the arm64 image — `makerom`/`bannertool` are x86_64 binaries
    and will SIGILL/exec-format-error on arm64.
  - Tool version + zip sha256 pins (both Dockerfile and CI must match; bump the hash with the
    version): **makerom v0.18.4** (v0.19.0 needs GLIBC_2.38; the `devkitpro/devkitarm` bookworm base
    ships 2.36 — v0.19.0 is broken there), **bannertool v1.2.2** (Epicpkmn11 fork; Steveice10's
    repo is archived/404).
  - CI (`release.yml`) runs on x86_64 GitHub runners — no platform flag needed; it installs makerom +
    bannertool natively and runs `make -C 3ds cia` directly.
  - Incremental `make -C 3ds` (no `clean`) recompiles only changed files; `clean` after Makefile/romfs changes.
- Verify romfs (`cacert.pem`) via the packaging log; `waystone.h` must stay unchanged
  (`git diff --stat ffi/include/waystone.h` empty). Local Docker images get GC-pruned (exit 125 = image
  gone) — rebuild from the Dockerfile.

### Releasing

Releases are cut by pushing ONE `vX.Y.Z` tag that ships both shells — the GitHub Actions workflow
`.github/workflows/release.yml` builds the 3DS (`devkitpro/devkitarm`, same toolchain as the Docker image)
and the Switch (`devkitpro/devkita64`) in parallel jobs, then a `publish` job creates the release and attaches
`waystone-3ds-spike.3dsx` (listed first), the installable `waystone-3ds-spike.cia`
(`make -C 3ds cia`, makerom + bannertool installed by the workflow) and `waystone.nro`:

1. Bump `WS_APP_VERSION` in **both** `3ds/source/version.h` and `switch/source/version.h` in one commit
   (CI fails if they differ or don't match the tag).
2. `git tag vX.Y.Z`.
3. `git push origin vX.Y.Z` — the workflow builds and publishes the release.
4. Verify on the GitHub release page that `waystone-3ds-spike.3dsx` (first), `waystone-3ds-spike.cia` and
   `waystone.nro` are all attached. The 3DS in-app updater picks its asset by run mode (`envIsHomebrew()`):
   under HBL/netload it replaces the `.3dsx`; as the installed CIA it downloads the `.cia` to
   `sdmc:/waystone/update.cia`, imports it via AM (`3ds/source/cia_install.cpp`), deletes the temp file and
   offers Restart now (`aptSetChainloaderToSelf`) / Later. The Switch updater matches the `.nro`.

The `.cia` is the installable artifact (Home Menu) and self-updates in place (same permanent title ID).

Note: the repo must be **public** — the updater queries the GitHub releases API unauthenticated.
