# Contributing to Waystone

## Workspace layout

```
core/        Pure-logic library: model, canonical ZIP, sha256, E2EE, conflict engine, adapters.
             No I/O of any kind — bytes in, decisions out. Compiles to any target.
desktop/     CLI shell: filesystem I/O, WebDAV client, exercises the full pipeline.
deploy/      Docker Compose for the dufs WebDAV backend.
design/      Shared design-system tokens (tokens.json). Consumed/ported by each shell.
docs/        Internal knowledge base (ephemeral specs, plans). Not user-facing.
```

Console and mobile shells (M2–M4) will live in `switch/`, `android/`, `3ds/`
when they land. Each requires its own toolchain — see below.

## Building and testing

The core library and desktop CLI use the standard Rust toolchain. No extra
toolchains are required for M1 work.

```sh
# Build everything
cargo build --workspace

# Run all tests
cargo test --workspace

# Lint
cargo clippy --workspace
```

All CI gates must pass before a PR is merged.

## Golden test vectors

`core/tests/golden_vectors.rs` encodes byte-exact behavior for the canonical
ZIP format, sha256 hashing, and E2EE encrypt/decrypt. The expected values are
hardcoded — not derived at test time.

**These vectors are the cross-platform determinism contract.** Every future
platform shell (Switch, Android, 3DS) must produce identical output for the
same inputs. Never change a vector without a deliberate, tracked spec update
and simultaneous updates to all shell test suites.

## Code style

- **Keep comments minimal.** Only explain non-standard patterns — never restate
  what the code does.
- TDD: write a failing test first, then implement. No production code without a
  failing test.
- The `core` crate must stay I/O-free. No filesystem, no networking, no `std::fs`,
  no `tokio`. Bytes in, decisions out.
- Use `cargo fmt` before committing.

## Console and mobile toolchains

The platform shells (M2–M4) each require toolchains outside the standard Rust
ecosystem:

| Shell | Toolchain |
|---|---|
| Switch (M2) | devkitPro + libnx; optionally `aarch64-switch-rs` / `cargo-nx` |
| Android (M3) | Android NDK + Kotlin; UniFFI for Rust↔Kotlin bindings |
| 3DS (M4) | devkitARM + libctru; optionally `ctru-rs` / `cargo-3ds` |

These shells are not part of the Cargo workspace and will have their own build
scripts. Desktop contributors do not need any of these installed.
