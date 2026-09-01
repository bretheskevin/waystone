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

**Quality:** 55 tests pass, clippy clean.

## Deferred (minor, non-blocking)
- Zeroize the WebDAV password on drop (the Vault master key already is).
- Remove or wire up the dead-code `read_remote_head` scaffolding in `desktop/src/pipeline.rs`.

## Next
- **M2 — Switch shell**: C++/libnx UI + save extraction, calling the Rust `core`
  over a **C ABI** (`cbindgen` + a `waystone-ffi` crate). Needs devkitPro.
- **M3 — Android shell**: Kotlin/Compose UI, core via **UniFFI** (SAF + OkHttp).
- **M4 — 3DS shell**: C++/libctru + citro2d.

All shells share one design system (`design/tokens.json`), rendered natively per
platform — premium and platform-appropriate, not a forced single skin.
