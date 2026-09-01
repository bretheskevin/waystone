# Waystone — Status & Roadmap

_Last updated: 2026-09-01_

Cross-platform game-save sync (backup **and** cross-device sync) spanning emulator
saves (mGBA, TWiLight++) and native installed-game saves (Switch, 3DS). See
[ARCHITECTURE.md](ARCHITECTURE.md) for the full design.

## Done

### Milestone 1 — core + desktop + backend
- **`core/`** (Rust, no-I/O engine): normalized model, byte-exact **deterministic ZIP**
  + sha256, mandatory client-side **E2EE** (Argon2id + XChaCha20-Poly1305 + HKDF +
  HMAC metadata obfuscation; master key zeroized on drop), **three-way conflict**
  resolution, and **jksv + mgba adapters**. Golden vectors lock cross-platform
  determinism.
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

### Milestone 2 (de-risk) — no_std + Switch target cross-compilation
- **`core/` + `ffi/` are genuinely `no_std + alloc`**: removed `zip`/`rand`/`thiserror`
  dependencies; STORED-zip reader inline; `getrandom` for entropy; manual error impls;
  `spin::Mutex` global error slot; `catch_unwind` removed (moot under `panic=abort`).
- **Switch target cross-compilation verified**: `cargo +nightly build -Zbuild-std=core,alloc
  --target aarch64-nintendo-switch-freestanding --features switch -p waystone-ffi` produces
  `target/aarch64-nintendo-switch-freestanding/debug/libwaystone_ffi.a` (32 MB).
  Unresolved symbols (`memalign`, `free`, `nx_getrandom`) are intentional — they
  resolve at the C++ libnx link step.
- **All host tests green**: 92 tests pass, golden vectors byte-identical, C header unchanged.

**Quality:** 92 tests pass, clippy clean.

## Deferred (minor, non-blocking)
- Zeroize the WebDAV password on drop (the Vault master key already is).
- Remove or wire up the dead-code `read_remote_head` scaffolding in `desktop/src/pipeline.rs`.

## Next
- **M2 (remaining) — Switch shell UI**: C++/libnx + borealis homebrew app in Docker
  (devkitPro). Link `libwaystone_ffi.a` via the Makefile, provide `memalign`/`free`/
  `nx_getrandom` symbols from newlib/libnx. Build the `.nro`.
- **M3 — Android shell**: Kotlin/Compose UI, core via **UniFFI** (SAF + OkHttp).
- **M4 — 3DS shell**: C++/libctru + citro2d.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
