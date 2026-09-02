# Waystone — Status & Roadmap

_Last updated: 2026-09-02_

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

**Quality:** 103 tests pass (11 new checkpoint core tests + 3 checkpoint FFI tests), clippy clean.

### Adapter source references
- **JKSV**: https://github.com/J-D-K/JKSV
- **mGBA**: https://github.com/mgba-emu/mgba
- **TWiLight Menu++**: https://github.com/DS-Homebrew/TWiLightMenu
- **nds-bootstrap**: https://github.com/DS-Homebrew/nds-bootstrap
- **Checkpoint**: https://github.com/FlagBrew/Checkpoint

## Next
- **M2 (remaining) — Switch shell: borealis UI + TLS + first on-hardware run**: the two-way save
  engine (push + pull/restore) is built and compile+link-verified end-to-end. Remaining: borealis
  UI (game list + config/credential entry, replacing the console driver), HTTPS/TLS for WebDAV, and
  running it on Switch hardware for the first time.
- **M3 — Android shell**: Kotlin/Compose UI, core via **UniFFI** (SAF + OkHttp).
- **M4 — 3DS shell**: C++/libctru + citro2d.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
