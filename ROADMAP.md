# Waystone — Status & Roadmap

_Last updated: 2026-09-08_

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
  (current save snapshotted before overwrite). Browse+restore only (delete/prune deferred). DRY:
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
- **M2 (remaining) — Switch shell: first on-hardware run**: the two-way save engine (push +
  pull/restore), verified HTTPS, the **borealis GUI vertical slice**, and the **full-parity screens**
  (runtime Setup / Unlock via swkbd, Settings, and the conflict inbox — see Done) are all built and
  compile+link-verified end-to-end in both GUI and `CONSOLE=1`. Remaining: the **first on-hardware run** —
  eyeball the borealis UI and exercise setup / unlock / sync / conflict-resolution on real Switch hardware.
- **M3 (adapter parity landed) — Android shell**: the **UniFFI binding foundation** is built and
  host-verified. A shared **`waystone-sync`** crate owns sync orchestration; **desktop** delegates to
  it; and **`waystone-mobile`** (uniffi 0.32) exposes a complete vertical slice to Kotlin — `Vault`,
  records, `SyncDecision`, a foreign `WebDav` trait (for OkHttp), `push_one`/`pull_one` (with
  `ConflictPolicy`), and **full adapter parity**: `jksv`/`mgba`/`twilight`/`checkpoint` normalize +
  to_native, with lossless `NormalizedSave` fidelity (`serial`/`rom_crc`/`confidence`). Committed
  Kotlin binding kept honest by an up-to-date test. Remaining: the **Kotlin/Compose UI** + Kotlin SAF
  (storage) & OkHttp (WebDAV) trait impls, and a first on-device run.
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
  `PS_GenerateRandomBytes`).
- **3DS verified HTTPS/TLS (compile+link)**: the 3DS WebDAV client now uses verified HTTPS, mirroring
  the Switch TLS slice exactly — `curl_apply_tls` (`SSL_VERIFYPEER=1`, `SSL_VERIFYHOST=2`,
  `CAINFO=romfs:/cacert.pem`) on every handle. The 3DS `3ds-curl` port is built against `3ds-mbedtls`,
  so the Mozilla CA bundle (reused byte-for-byte from `switch/romfs/cacert.pem`) is read directly from
  romfs — no system-store augmentation. Added romfs to the 3DS build (`ROMFS := romfs`, `_3DSXFLAGS
  --romfs`, `romfsInit`/`romfsExit` in `main.cpp`; `3dsxtool` needs `--smdh` alongside `--romfs`). CA
  bundle bytes confirmed embedded in the `.3dsx`. **Compile+link verified; not yet run on hardware
  (clock must be correct for cert date validation).** Remaining for M4: the citro2d GUI and a first
  on-hardware run.
- **Shared WebDAV net layer (DRY)**: the duplicate libcurl WebDAV client (`net.h`/`net.cpp`, incl. the
  shared `curl_apply_tls`) was extracted from both console shells into a single **`shell-common/net.{h,cpp}`**
  consumed by both via the existing `../shell-common` Makefile glob (no Makefile change needed — `net.o`
  basename already in the Switch `OUR_ENGINE_OBJS`). Zero logic change; all three builds green (Switch GUI +
  `CONSOLE=1`, 3DS). The shared file keeps `#include <sys/select.h>` — required by devkitARM's `curl/multi.h`
  (`fd_set`), harmless on devkitA64.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
