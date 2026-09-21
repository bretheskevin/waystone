# Waystone — Status & Roadmap

_Last updated: 2026-09-18_

Cross-platform game-save sync (backup **and** cross-device sync) spanning emulator
saves (mGBA, TWiLight++) and native installed-game saves (Switch, 3DS). See
[ARCHITECTURE.md](ARCHITECTURE.md) for the full design.

## Done

### Milestone 1 — core + desktop + backend
- **`core/`** (Rust, no-I/O engine): normalized model, byte-exact **deterministic ZIP**
  + sha256, mandatory client-side **E2EE** (Argon2id + XChaCha20-Poly1305 + HKDF +
  HMAC metadata obfuscation; master key zeroized on drop), **three-way conflict**
  resolution, and **jksv + mgba + twilight (NDS) + checkpoint (Switch/3DS) adapters**
  ([FlagBrew/Checkpoint](https://github.com/FlagBrew/Checkpoint)). Golden vectors lock
  cross-platform determinism.
- **`desktop/`** CLI: config, WebDAV client, sync pipeline, `init/push/pull/status`.
- **`deploy/`**: `dufs` WebDAV backend for Dokploy (Traefik auto-HTTPS).

### Milestone 1.1 — real cross-device sync
- **Fold-based pull**: `PROPFIND` lists every device's head → decrypt → `fold_heads`
  → correct three-way decision. Cross-device sync now works end-to-end.
- **WebDAV auth**: `--username` + `WAYSTONE_WEBDAV_PASSWORD` (env) or interactive prompt.
- ✅ **Verified live**: two-device push→pull against a real `dufs` container.

### Milestone 2 (foundation) — Switch FFI
- **`ffi/`** (`waystone-ffi` crate): stable C ABI exposing core's packaging,
  hashing, E2EE, conflict resolution, and adapter logic over JSON strings +
  `{ptr,len}` byte buffers. Opaque `WsVault*` handle (zeroized on free).
- **`decide_pull` promoted to `core::conflict`**: single source of truth for the
  three-way pull decision, shared by desktop and FFI.
- **cbindgen-generated `include/waystone.h`**: committed header with freshness
  test — the C++ shell `#include`s this directly.
- **Integration tests**: golden-vector parity through the JSON/base64 FFI
  round-trip, crypto round-trips, adapter round-trips, null-safety.
- **twilight (NDS) FFI**: `ws_twilight_normalize` + `ws_twilight_to_native` exposed
  over the C ABI; `ffi/include/waystone.h` regenerated and header-freshness test green.
- **checkpoint (Switch/3DS) adapter**: `ws_checkpoint_normalize` + `ws_checkpoint_to_native`
  exposed over the C ABI; Switch `0x<16-hex> <Name>` and 3DS `0x<5-hex> <Name>` folder
  layouts parsed; title ID extracted as convergence key (Strong) with no-prefix fallback (Weak).
  Source: [FlagBrew/Checkpoint](https://github.com/FlagBrew/Checkpoint).

### Milestone 2 (de-risk) — no_std + Switch target + libnx link spike
- **`core/` + `ffi/` are genuinely `no_std + alloc`**: removed `zip`/`rand`/`thiserror`
  dependencies; STORED-zip reader inline; `getrandom` for entropy; manual error impls;
  `spin::Mutex` global error slot; `catch_unwind` removed (moot under `panic=abort`).
- **Switch target cross-compilation verified**: `cargo +nightly build -Zbuild-std=core,alloc
  --target aarch64-nintendo-switch-freestanding --features switch -p waystone-ffi` produces
  `target/aarch64-nintendo-switch-freestanding/debug/libwaystone_ffi.a` (32 MB).
  Unresolved symbols (`memalign`, `free`, `nx_getrandom`) are intentional — they
  resolve at the C++ libnx link step.
- **libnx link spike verified (compile+link)**: `libwaystone_ffi.a` (release,
  `aarch64-nintendo-switch-freestanding`, no custom-target fallback needed) links cleanly
  into a devkitPro/libnx C++ homebrew. Output: `switch/waystone-spike.nro` (~215 KB), built
  via `switch/build.sh` inside the `waystone-switch` Docker image (devkitpro/devkita64 +
  rustup nightly + rust-src). The demo drives a full C-ABI round-trip (`ws_vault_init` →
  `ws_canonical_zip`/`ws_content_hash` → `ws_vault_encrypt_blob` → `ws_vault_decrypt_blob`
  → `ws_unzip`); `nx_getrandom` wired to `randomGet`. **Compile+link verified — not yet run
  on hardware.**
- **All host tests green**: 92 tests pass, golden vectors byte-identical, C header unchanged.
- **WebDAV networking spike (compile+link)**: switch-curl (`dkp-pacman -S switch-curl`)
  installs into the `waystone-switch` Docker image. `net_webdav_probe()` in
  `switch/source/net.cpp` issues PUT, GET, PROPFIND(Depth:1) via libcurl with basic auth
  over plain HTTP, mirroring `desktop/src/webdav.rs` verb shapes. The `.nro` links with
  `-lcurl` resolved alongside `-lwaystone_ffi` and `-lnx`. **Compile+link verified — plain
  HTTP only (no TLS this spike); not yet run on hardware.**
- **Switch save-engine (push) slice (compile+link)**: the real save-extraction + push pipeline in
  `switch/source/` — libnx `ns` title enumeration + `account`/`fsdevMountSaveData` save-mount →
  `RawTreeDto` (base64 + JSON via vendored jsmn) → `ws_jksv_normalize` → `ws_package` →
  `ws_vault_encrypt_blob` → WebDAV PUT of blob + encrypted heads/history, mirroring
  `desktop/src/pipeline.rs`. `net.cpp` refactored into reusable
  `webdav_put/get/exists/mkcol/mkdir_p/propfind` verbs. Console-driven (borealis deferred).
  **Compile+link verified into the `.nro`; not yet run on hardware.**
- **Switch pull/restore slice (compile+link)**: the reverse (two-way sync) path in `switch/source/`,
  mirroring desktop `do_pull_save` — PROPFIND `heads/` → `ws_vault_decrypt_heads` → `ws_decide_pull`
  (NewestWins) → GET blob → `ws_vault_decrypt_blob` → `ws_unzip` → base64-decode → write back to the
  `fsdevMountSaveData` mount + `fsdevCommitDevice`. Added `base64_decode` and a `make_base_path`
  helper shared with push. **Compile+link verified into the `.nro`; not yet run on hardware.**
- **Switch TLS/HTTPS (compile+link)**: verified HTTPS for the WebDAV client — `curl_apply_tls`
  (`SSL_VERIFYPEER=1`, `SSL_VERIFYHOST=2`, `CAINFO=romfs:/cacert.pem`) on every handle. switch-curl's
  libnx TLS backend uses the Switch system CA store (ISRG Root X1 / Let's Encrypt on fw ≥10.1.0),
  augmented by a Mozilla CA bundle embedded via romfs (`ROMFS := romfs`, `--romfsdir`). CA bundle
  bytes confirmed present in the `.nro`. **Compile+link verified; not yet run on hardware (clock
  must be correct for cert date validation).**
- **Switch borealis GUI vertical slice (compile+link)**: replaces the console driver as the default
  entry point with a **borealis** UI (deko3d backend) — `app_main.cpp` + a programmatic
  `TitleListActivity` (game list + "Sync all" action) driving `push_title`/`pull_title` on a
  `SyncController` worker thread, with a live status label polled via `brls::RepeatingTimer`. borealis
  is vendored as a pinned submodule (`switch/lib/borealis` @ `20e2d33b`, `library/borealis.mk`
  fragment); the romfs is assembled at build time from the submodule (fonts/i18n/xml + `uam`-compiled
  deko3d shaders), with only `cacert.pem` committed. The console driver is kept byte-for-byte behind
  `make CONSOLE=1`. **Compile+link verified in both modes and romfs embedding confirmed via the
  packaging log; the UI cannot be rendered on the host — not yet eyeballed on hardware.**

- **Switch full-parity screens (compile+link)**: the borealis shell is now **self-sufficient** — every
  compile-time credential `#define` is gone, replaced by a runtime **session model** mirroring the desktop
  TUI. `app_main` routes by `keys.json` presence: first-run **Setup** (server / username / WebDAV password /
  vault passphrase via a thin libnx **`swkbd`** wrapper; overwrite guard; `ws_vault_init`; recovery key →
  `sdmc:/waystone/recovery-<device>.txt`) vs. returning-user **Unlock** (passphrase or recovery key + WebDAV
  password). A **Settings** screen (server / conflict-policy / safety-backup / username; device_id read-only)
  persists `config.json`, and a **Conflict inbox** runs a dedicated scan (`ws_decide_pull` with **Prompt**
  policy, collecting the `conflict_needs_input` cases the engine previously skipped) resolved keep-local /
  keep-remote on a worker thread. Native borealis look, **Waystone-tinted** (indigo/cyan theme override — a
  one-line borealis `ThemeValues::addColor` override, applied via a committed build-time patch
  (`switch/patches/borealis-theme-tint.patch`, idempotently applied by the Makefile — submodule stays
  pinned + clean); the excluded `swkbd.cpp` is left untouched). New shared **`shell-common/wsconfig`** (config load/save, reusable by 3DS) + a DRY refactor of
  `sync.cpp` (`scan_save_decision` + `restore_remote_save` shared by `pull_title` and the conflict controller);
  secrets are RAM-only and zeroized (shared `secure_clear`). Compile+link verified in **both GUI and
  `CONSOLE=1`** modes; 3DS build unaffected; `waystone.h` unchanged; reviewed (two rounds, DRY + concurrency +
  secret-hygiene). **Not yet run on hardware.**

- **Switch FIRST ON-HARDWARE RUN + guided onboarding wizard**: the borealis shell was run on a real
  Switch for the first time (SD-card deploy via OpenMTP, full application mode) — it boots into the GUI and
  swkbd works on-device. Driven by that testing, the first-run UX was redesigned from an auto-fire keyboard
  sequence into a **step-by-step wizard** (Setup + Unlock): one field per step with progress dots, current
  value (secrets masked), inline hint + validation, RB/LB navigation, controlled `swkbd` editing, and a
  full-screen "save your recovery key" confirmation that can't be skipped. DRY via a shared `WizardActivity`
  base + `WizardRenderer` + `vault_helpers`. Compile+link verified (GUI + `CONSOLE=1`); **on-device re-test of
  the wizard in progress.**

### Milestone 2.5 — console on-hardware bring-up + UX parity (2026-09-09 → 2026-09-18)
- **Switch NRO boots on hardware**: force-static the Rust FFI lib (RELATIVE-only relocations) fixed the
  on-device boot. The wizard was hardened against real-hardware crashes: borealis use-after-free fixes
  (field editing, `reload_steps`, orphaned views, secret SSO remnant), a multi-field wizard layout with
  confirm button + slide-fade animation, and an AppletFrame footer with real controller button glyphs
  (A/RB/LB/+, NintendoExt PUA). Create-Vault no longer black-screens — the Argon2 KDF runs on a worker
  thread with progress + error surfacing.
- **Headless borealis desktop preview harness** (`switch/preview/`): renders the real wizard/dashboard UI
  to PNG screenshots without hardware — borealis compiled from source against GLFW with swkbd/FFI/saves
  stubs, a NintendoExt glyph font generated from real Switch button shapes, and a dashboard mode with
  fixture titles. Preview fonts are kept out of the shipped `.nro` romfs.
- **No-internet handling (both consoles)**: connectivity gate at startup + before sync, curl timeouts, a
  `NoInternetActivity`/`NoInternetScreen` with retry, and the message strings DRY-extracted to shared
  `shell-common/net_status.h` helpers.
- **Persistent login (both consoles)**: an MDK-based session store (`session_store`) enables auto-unlock
  on launch — Switch gained auto-unlock + a Settings "Log out", 3DS gained the same plus a CFGU-backed
  device key (`ctr_device_key`) so no passphrase is needed on return visits. Secrets zeroized; logout
  clears the store and frees the vault.
- **3DS citro2d GUI (M4 core slice)**: the 3DS shell is no longer console-only — a citro2d/citro3d UI
  with the same screen model as Switch (`Screen` stack, theme from `design/tokens.json`, widgets, swkbd
  wrapper): onboarding wizard (Setup → recovery key), Unlock (passphrase or recovery key), Loading
  (auto-unlock), and a title list driving push/pull sync. Default build is the GUI; `CONSOLE=1` keeps the
  text driver. Compile+link verified in both modes.
- **Conflict-resolution inbox at parity (both consoles)**: 3DS gained the conflict inbox (dedicated
  Prompt-policy scan on a worker thread, dual-screen UI, keep-local/keep-remote resolution); Switch gained
  a confirm-before-keep-remote gate (focusable rows + amber confirm banner — also fixed a pre-existing
  bug where non-focusable label rows made selection stuck at 0).
- **On-device recovery surfaces (both consoles)**: the desktop's history and snapshot restore arcs are
  now on-device — remote-history browse + restore and local-snapshot browse + restore on both Switch and
  3DS, going through the same safety-backup guard as desktop.
- **Dashboard redesign (both consoles)**: Switch's title list is now an AppletFrame with 56×56 icon +
  real game name rows (icons extracted from NACP and cached to `sdmc:/waystone/icons/`), wired nxlink
  logging; 3DS reached parity — real game names + 48×48 SMDH icons (UTF-16LE short-desc, tiled RGB565 →
  `C3D_Tex` on the render thread), no title IDs shown anywhere. 3DS layout flipped to list-on-top /
  action-panel-on-bottom.
- **3DS two-way extdata backup + restore**: `extract_save_json` also walks `ARCHIVE_EXTDATA` (Checkpoint
  `TitleQuirks` extdata-ID table, GPLv3-clean transcription), emitting a second independent save group
  (`3ds/<game>/extdata`) that rides the existing sync loops with **zero core/FFI change** (4 new
  checkpoint.rs tests). Restore routes by archive kind — extdata skips the commit + secure-value delete,
  matching Checkpoint. Snapshot restore is extdata-aware. **Runtime extdata read/write pending
  on-hardware.**
- **Deploy**: the `dufs` WebDAV service joins `dokploy-network` so Traefik routes it over HTTPS.

### Milestone 4 (foundation) — 3DS engine, TLS, shared shell-common
- **3DS save-sync engine (host-verified, compile+link)**: a `3ds/` shell (devkitARM/libctru, Docker image
  `waystone-3ds`) mirrors the Switch M2 engine console-driven: libctru title enumeration + FS-archive
  savedata extraction → `ws_checkpoint_normalize("3ds", …)` → package → vault-encrypt → WebDAV **two-way
  sync** (push AND pull/restore): pull mirrors the Switch slice — PROPFIND heads → `ws_decide_pull`
  (NewestWins) → GET+decrypt blob → `ws_unzip` → `write_save_files` (FS-archive write-back +
  `ARCHIVE_ACTION_COMMIT_SAVE_DATA`; restore correctness cross-checked against Checkpoint `io::restore` —
  root wipe, no partial commits, secure-value delete). Cross-compiles to `armv6k-nintendo-3ds` into a
  `.3dsx`, with a 3DS `getrandom` shim over `PS_GenerateRandomBytes`.
- **DRY generalization for the second console**: FFI/core no_std runtime + entropy generalized
  (`console_runtime`/`console_entropy`, gated `any(feature = "switch", feature = "3ds")`, Switch
  byte-identical), and `json`/`jsmn`/`base64`/`snapshot`/`net` extracted to a shared **`shell-common/`**
  consumed by both console Makefiles — including the single shared libcurl WebDAV client
  (`shell-common/net.{h,cpp}`) with one `curl_apply_tls`.
- **3DS verified HTTPS/TLS (compile+link)**: mirrors the Switch TLS slice — `curl_apply_tls`
  (`SSL_VERIFYPEER=1`, `SSL_VERIFYHOST=2`, `CAINFO=romfs:/cacert.pem`) on every handle; `3ds-curl` is
  built against `3ds-mbedtls`, so the Mozilla CA bundle (reused byte-for-byte from
  `switch/romfs/cacert.pem`) ships via romfs. CA bundle bytes confirmed embedded in the `.3dsx`.

**Quality:** `cargo test --workspace` green throughout (103+ tests — checkpoint extdata and mobile
adapter-parity tests added since), clippy clean; every console slice compile+link-verified in both GUI
and `CONSOLE=1` modes.

### Adapter source references
- **JKSV**: https://github.com/J-D-K/JKSV
- **mGBA**: https://github.com/mgba-emu/mgba
- **TWiLight Menu++**: https://github.com/DS-Homebrew/TWiLightMenu
- **nds-bootstrap**: https://github.com/DS-Homebrew/nds-bootstrap
- **Checkpoint**: https://github.com/FlagBrew/Checkpoint

### Desktop TUI (`waystone tui`)
- Interactive `ratatui` v1 + `crossterm` dashboard over the existing async pipeline, launched by a
  new `waystone tui` subcommand (CLI subcommands unchanged). **Elm-style architecture**: pure `App`
  state + `Msg` + `update()` reducer (19 headless unit tests) + `ui()` render + a `tokio::select!`
  loop merging `crossterm::EventStream` and an `mpsc` fed by background push/pull tasks (live
  progress). Full-CRUD **sync targets** persisted in `WaystoneConfig` (`targets`, `#[serde(default)]`
  back-compat); per-target sync status; **session unlock modal** (vault passphrase + WebDAV password,
  masked, in-memory, zeroized). Colors from `design/tokens.json` (`tui/theme.rs`) for design parity.
  Shared `desktop/src/helpers.rs` gives the CLI and TUI one adapter/pipeline code path.
- **Conflict-resolution inbox (TUI v2)**: status refresh detects divergences with
  `ConflictPolicy::Prompt` (regardless of config); a `Screen::Conflicts` inbox lists every diverged
  save with metadata (local vs remote `{device_id, hash, mtime}`), resolved with **keep local**
  (push) / **keep remote** (force pull-by-hash → restore) / cancel — non-destructive (history is
  append-only). The restore tail is now one shared `helpers::restore_save_from_blob` (CLI + TUI).
- **In-TUI setup + settings (TUI v3)**: `waystone tui` is now fully self-sufficient (no CLI needed).
  First-run **Setup** (`Screen::Setup`, shown when no server is configured) creates the vault entirely
  in the TUI — server/creds/passphrase form → **overwrite guard** (GET `/keys.json`: refuse + redirect
  to unlock if a vault already exists, else `Vault::init` → PUT) → one-time **recovery-key modal**
  (`Overlay::RecoveryKey`, save-to-file at `<config_dir>/recovery-<device_id>.txt` 0600 + explicit ack,
  then zeroized). A **Settings** screen (`Screen::Settings`, `S`) edits server/policy/username
  (device_id read-only); a server change clears session creds. Secrets masked, non-Debug, zeroized.
  **Deferred to v4**: content/file-level conflict diff, "keep both", destructive re-init /
  change-passphrase. _(recovery-key unlock in the TUI — done, see below.)_
- **Safety backup before restore**: a default-on `safety_backup` setting (toggle in the Settings
  screen) snapshots the current local save to `<config_dir>/backups/<group_key>/<ts>/` before ANY
  action that overwrites local (CLI/TUI pull + conflict "keep remote"); if the snapshot fails the
  restore is **aborted** (hard precondition), so an un-pushed local can't be lost. Local-only,
  offline-safe (shared `helpers::safety_snapshot`; `group_key` path-traversal-sanitized). **Switch +
  3DS honor the same** in-engine: a shared `shell-common/snapshot.cpp` snapshots the current save to
  `sdmc:/waystone/backups/<key>/<ts>/` before `write_save_files` in each `pull_title` (always-on,
  skip-that-title on failure) — compile+link verified on both.
- **History-restore (browse & restore any past version)**: the append-only server `history/` is now
  **readable and recoverable**, closing the gap where history was written on every push but never read.
  A shared **`waystone_sync::list_history`** (PROPFIND `history/` + `decrypt_heads`, mirroring
  `read_remote_heads`) lists every past `{timestamp, device_id, hash, mtime}` newest-first (timestamp
  parsed from the `{ts}-{device_id}.json` filename). Both desktop surfaces restore any version **locally**
  — remote heads/history untouched; a later explicit push propagates it — through the safety-backup guard:
  a new CLI **`waystone history list|restore`** (path-based like `pull`/`status`, `<ts|hash-prefix>`
  selector via a pure `resolve_history_selector`) and a TUI **`Screen::History`** picker (`h` on the
  dashboard, mirroring the conflict inbox). The restore tail is now **DRY-unified**: one
  `helpers::guarded_restore` (safety-snapshot → fetch-blob → restore) backs CLI pull, TUI pull, conflict
  keep-remote, **and** both new history-restore paths — 3 open-coded copies collapsed to 1. Host-verified:
  `cargo test --workspace` green (14 new tests), clippy/fmt clean, reviewed CLEAN (0 findings).
- **Local-snapshot restore (offline)**: the local safety-backup snapshots at
  `<config_dir>/backups/<key>/<ts>/` — previously write-only — are now browsable and restorable, closing
  the last leg of the recovery arc (an un-pushed local you accidentally overwrote is recoverable). Because
  a snapshot is a native-layout raw copy, restore is a **guarded file-copy back** (overwrite-merge) needing
  **no vault, no WebDAV, no passphrase** — fully **offline**, usable with the server down or before a vault
  exists. A separate **`waystone snapshots list|restore`** CLI + a TUI **`Screen::Snapshots`** (`b` on the
  dashboard, deliberately **not** creds-gated, unlike history's `h`) both go through the safety guard
  (current save snapshotted before overwrite). Browse+restore only. **Snapshot prune landed (both
  consoles, shared `shell-common/snapshot.cpp`): after each successful snapshot write, only the 10
  newest per game are kept** — non-fatal, strict `YYYYMMDDTHHMMSSZ` name filter so foreign/partial
  entries are never deleted. DRY:
  `sanitize_group_key` + `copy_tree` extracted from `snapshot_save_dir` and single-sourced across
  writer/reader; hermetic testable cores take an explicit `backups_root`. Host-verified: `cargo test
  --workspace` green (19 new tests: 9 helpers + 4 CLI + 6 TUI), clippy/fmt clean, reviewed CLEAN (0 findings).
- **Recovery-key unlock in the TUI**: the TUI unlock overlay can now unlock with the **recovery key**, not
  just the passphrase — closing a lockout gap (a forgotten passphrase previously locked you out of the TUI
  even though the recovery key was generated + saved at setup). A **Ctrl+R** toggle swaps the secret field
  between _Passphrase_ and _Recovery key_ (same overlay, same WebDAV field, same flow; the key is masked);
  `Cmd::AttemptUnlock`/`try_unlock` gained a `recovery` flag that branches to the existing tested
  `core::crypto::unlock_with_recovery`. No core changes; secret zeroized on toggle/cancel/submit. Just-unlock
  (setting a new passphrase stays the deferred change-passphrase feature). Host-verified: `cargo test
  -p waystone-desktop` green (4 new reducer tests), clippy/fmt clean, reviewed CLEAN (0 findings).

## Next
- **M2 (remaining) — Switch shell: on-device validation pass**: the first on-hardware run happened and
  drove the wizard redesign + boot/UAF/KDF fixes (see Done). Remaining: a full on-device pass over the
  redesigned shell — wizard setup/unlock, auto-unlock, dashboard sync, conflict inbox, history/snapshot
  restore — on real Switch hardware.
- **M3 (adapter parity landed) — Android shell**: the **UniFFI binding foundation** is built and
  host-verified. A shared **`waystone-sync`** crate owns sync orchestration; **desktop** delegates to
  it; and **`waystone-mobile`** (uniffi 0.32) exposes a complete vertical slice to Kotlin — `Vault`,
  records, `SyncDecision`, a foreign `WebDav` trait (for OkHttp), `push_one`/`pull_one` (with
  `ConflictPolicy`), and **full adapter parity**: `jksv`/`mgba`/`twilight`/`checkpoint` normalize +
  to_native, with lossless `NormalizedSave` fidelity (`serial`/`rom_crc`/`confidence`). Committed
  Kotlin binding kept honest by an up-to-date test. Remaining: the **Kotlin/Compose UI** + Kotlin SAF
  (storage) & OkHttp (WebDAV) trait impls, and a first on-device run.
- **M4 (engine + citro2d GUI landed) — 3DS shell**: engine, verified HTTPS/TLS, persistent login,
  conflict inbox, no-internet gate, history/snapshot restore, dashboard with SMDH icons, and two-way
  extdata are all built and compile+link-verified (see Done). **TWL/DSiWare enumeration + title
  pre-filtering have landed as well** (TWL-only scope by decision — gamecard and NAND system saves
  descoped; Checkpoint `isSystemExcluded` filter + TWL accessibility probe, `SaveTwl` routing with
  Checkpoint-exact write guards, `waystone.h` unchanged). Remaining, codeable without hardware:
  - **Extended enumeration** (descoped 2026-09-21, reference layouts already transcribed from
    Checkpoint): gamecard (`MEDIATYPE_GAME_CARD` + `FSUSER_GetCardType`) and NAND system saves
    (`ARCHIVE_SYSTEM_SAVEDATA`).
  Remaining, needs hardware: the **first on-hardware run** of the citro2d GUI, runtime extdata and
  TWL/DSiWare extract/restore validation, and a visual check of SMDH icon rendering (flip
  `subtex.top`/`bottom` in `3ds/source/ui/icon_tex.cpp` if icons render upside-down). 3DS clock
  must be correct for cert date validation.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
