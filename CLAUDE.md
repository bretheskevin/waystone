# Waystone — project-wide conventions

Cross-platform save-sync (Rust `core`/`ffi` + desktop, Switch, 3DS, Android/mobile shells).
Per-shell specifics live in `switch/CLAUDE.md` and `3ds/CLAUDE.md`.

## Git — work directly on `main`

Never create branches (no feature branches, no worktrees). All work is committed directly on `main`.

## Logging — instrument liberally so debugging is easy

Always add generous logging to the code. Debuggability beats terseness here: the console
shells can only be observed through their stdout stream (nxlink / 3dslink), so if a code path
isn't logged, on-hardware failures are invisible.

- **Log both success and failure paths** of every fallible operation — network I/O, save-data
  mount/commit, FFI calls across the Rust⇄C++ boundary, vault unlock/create, and each state
  transition (sync phases, wizard steps, activity push/pop).
- **Tag every console log line** with a bracketed subsystem prefix so streams are greppable:
  `[vault]`, `[sync]`, `[net]`, `[saves]`, `[titles]`, `[ui]`. These print via `printf` and
  surface over nxlink/3dslink.
- **Log entry + key parameters + outcome** for anything that can hang or crash (blocking network
  calls, Argon2 KDF, save walks) — a trailing "done"/"failed (err=N)" line turns a silent hang
  into a located one.
- In Rust `core`/`ffi`, log through the crate's existing facility (don't invent a second one —
  DRY). On the console shells, keep the tagged-`printf` pattern already in use.
- Logs are not comments: the global "keep comments minimal" rule does **not** discourage logging.
