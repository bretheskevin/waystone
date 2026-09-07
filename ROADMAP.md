# Waystone — Status & Roadmap

_Last updated: 2026-09-07_

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

**Quality:** 103 tests pass (11 new checkpoint core tests + 3 checkpoint FFI tests), clippy clean.

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
  change-passphrase, recovery-key unlock in the TUI.
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

## Next
- **M2 (remaining) — Switch shell: full UI + first on-hardware run**: the two-way save engine
  (push + pull/restore), verified HTTPS, and a **borealis GUI vertical slice** (title list + live
  sync; console driver kept behind `CONSOLE=1`) are built and compile+link-verified end-to-end.
  Remaining: eyeball the borealis UI on Switch hardware (first on-hardware run), then the full-parity
  screens — runtime setup / credential entry via swkbd (re-enables the excluded `swkbd.cpp`),
  conflict-resolution inbox, and settings — mirroring the desktop TUI.
- **M3 (foundation landed) — Android shell**: the **UniFFI binding foundation** is built and
  host-verified. A new shared **`waystone-sync`** crate owns the sync orchestration (a sync `WebDav`
  trait + `push_one`/`pull_one`/`fetch_blob`, reusing `core`); **desktop was refactored** to delegate
  to it (blocking bridge via `spawn_blocking`, 64 tests still green); and a new **`waystone-mobile`**
  UniFFI crate (uniffi 0.32) exposes a vertical slice to Kotlin — `Vault`, records, `SyncDecision`,
  a foreign `WebDav` trait (Kotlin implements OkHttp), and `jksv_normalize`/`push_one`/`pull_one`,
  with committed generated Kotlin kept honest by an up-to-date test. `cargo test/clippy/fmt` green
  workspace-wide. Remaining: the Kotlin/Compose UI + Kotlin SAF (storage) & OkHttp (WebDAV) trait
  impls; then full-parity surface (all adapters, packaging, a policy parameter on `pull_one`, richer
  `NormalizedSave` fidelity) and a first on-device run.
- **M4 (engine foundation landed) — 3DS shell**: the 3DS save-sync ENGINE is built and
  host-verified (compile+link via a new devkitARM Docker image `waystone-3ds`). A new `3ds/` shell
  (devkitARM/libctru) mirrors the Switch M2 engine console-driven: libctru title enumeration +
  FS-archive savedata extraction → `ws_checkpoint_normalize("3ds", …)` → package → vault-encrypt →
  WebDAV **two-way sync** (push AND pull/restore) over plain HTTP: pull mirrors the Switch
  slice — PROPFIND heads → `ws_decide_pull` (NewestWins) → GET+decrypt blob → `ws_unzip` →
  `write_save_files` (FS-archive write-back + `ARCHIVE_ACTION_COMMIT_SAVE_DATA`). To keep it DRY, the
  FFI/core no_std runtime + entropy were generalized (`console_runtime`/`console_entropy`, gated
  `any(feature = "switch", feature = "3ds")`) and `json`/`jsmn`/`base64` extracted to a shared
  `shell-common/` consumed by both console shells; the Switch build stayed green throughout.
  Cross-compiles to `armv6k-nintendo-3ds` into a `.3dsx` (with a 3DS `getrandom` shim over
  `PS_GenerateRandomBytes`). Remaining: TLS/HTTPS, the citro2d GUI, and a first on-hardware run.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
