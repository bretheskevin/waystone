# Waystone — Architecture

> Cross-platform game-save sync: back up and share saves across Nintendo Switch,
> Android, and 3DS, spanning emulator saves (mGBA, TWiLight++) and native
> installed-game saves. Public open-source, self-hosted backend.

---

## 1. Locked decisions

| Dimension | Decision |
|---|---|
| Audience | Public open-source from day one |
| Core job | Backup **and** cross-device sync, both core in v1 |
| v1 sources | Native Switch + mGBA (emulator) together |
| v1 devices | Switch + Android + 3DS on-device apps, + desktop CLI utility |
| Backend model | Self-host per user (BYO server) |
| Backend impl | **Dumb WebDAV** (`dufs`) on Dokploy; no custom server logic |
| Stack | **Rust pure core (no I/O) + native shells per platform** |
| Extraction | On-device homebrew does its own save extraction |
| Transport | On-device apps sync over WiFi directly to WebDAV |
| Conflict policy | Configurable; default **newest-wins + archive loser** |
| Game matching | Strong IDs auto-group; **filename-only matches confirmed once** |
| Save states | **Backed up per-device, never cross-shared** |
| Encryption | **Mandatory client-side E2EE** (passphrase + recovery key) |
| Design language | Premium, **native to each platform**, one shared design system |
| Desktop form | Thin CLI/TUI utility for v1 (GUI later) |

---

## 2. Architecture

```
                 ┌─────────────────────────────────────────┐
                 │  core/  (Rust, no_std + alloc, ZERO I/O) │
                 │  • normalized model (SaveId, SaveEntry…) │
                 │  • canonical deterministic ZIP           │
                 │  • sha256 content hashing                │
                 │  • E2EE (Argon2id + XChaCha20-Poly1305)  │
                 │  • conflict / heads three-way algebra    │
                 │  • adapter FORMAT logic (parse/emit)     │
                 │  exposed: C ABI (cbindgen) + UniFFI      │
                 └─────────────────────────────────────────┘
                     ▲          ▲          ▲          ▲
   native shells own ALL I/O + networking + UI + extraction:
   ┌──────────────┐ ┌──────────┐ ┌────────────┐ ┌─────────────┐
   │ switch/      │ │ 3ds/     │ │ android/   │ │ desktop/    │
   │ C++/libnx    │ │ C++/     │ │ Kotlin +   │ │ Rust CLI/TUI│
   │ save-mount,  │ │ libctru  │ │ Compose,   │ │ (uses core  │
   │ sockets+TLS, │ │          │ │ SAF+OkHttp │ │  directly,  │
   │ borealis UI  │ │ citro2d  │ │ →core via  │ │  no FFI)    │
   │              │ │          │ │ UniFFI     │ │             │
   └──────────────┘ └──────────┘ └────────────┘ └─────────────┘
                     │ WiFi / HTTPS
                     ▼
        ┌────────────────────────────────────┐
        │ dumb WebDAV (dufs) on user's Dokploy│
        │ Traefik → automatic Let's Encrypt   │
        └────────────────────────────────────┘
```

**Principle:** the correctness-critical logic (packaging, hashing, encryption,
conflict) is written **once** in Rust and must behave byte-identically on every
platform. It does **no I/O** — bytes in, decisions out — so it compiles cleanly
to every target (no networking / no_std headaches). Everything with I/O
(networking, filesystem, save extraction, UI) lives in the platform-native shell.

The FFI boundary passes **byte buffers and decisions**, never sockets or handles.

---

## 3. Data model (core)

```rust
enum SystemId { Switch, ThreeDS, Nds, Gba, Gbc, Gb, /* extensible */ }

enum SaveKind {
    Battery,    // raw SRAM/.sav → cross-device sync
    SaveState,  // emulator snapshot → per-device backup only
    Native,     // console save archive (folder) → switch↔switch etc. only
}

struct GameRef {
    key: String,              // canonical grouping key
    display_name: String,
    confidence: Confidence,   // Strong | Weak(filename)
    title_id: Option<String>, // Switch/3DS
    serial:   Option<String>, // cart gamecode, e.g. "AGB-BPEE"
    rom_crc:  Option<String>, // from ROM header when present
}

struct SaveId { source: String, system: SystemId, game: GameRef, slot: String, kind: SaveKind }

struct ContentRef {            // metadata only, no bytes
    hash: String,              // sha256 of the canonical (plaintext) zip
    size: u64,
    files: Vec<FileRef>,       // { path, size, hash }
}

struct SaveEntry {
    id: SaveId,
    group_key: String,         // `${system}/${game.key}/${slot}`
    portable: bool,            // battery/native = true, savestate = false
    content: ContentRef,
    mtime: String,             // ISO 8601
}
```

**Grouping key = `system/game.key/slot`** — `source` deliberately excluded so the
same game converges across sources/devices; `system` included so incompatible
saves can never cross-load. Non-portable entries (save states) are namespaced by
`deviceId` in the slot so they back up but never cross-share.

**`game.key` resolution (strongest first):** `title_id` (native) → ROM
`serial`/`rom_crc` (emulator, when ROM present) → normalized filename (**Weak**).
Weak matches across devices are grouped only after a **one-time user confirmation**
("these are the same game"), remembered thereafter.

---

## 4. Canonical packaging + hashing

- Every save (single file or folder) is normalized to a **deterministic ZIP**:
  entries sorted by path, timestamps zeroed, fixed compression settings — so
  identical content yields an identical archive and hash on any platform.
- `ContentRef.hash = sha256(canonical_plaintext_zip)`.
- Content-addressed: blobs are immutable; the plaintext hash is the identity used
  for dedup and change detection (see §6 for how it's obfuscated on the server).

---

## 5. Encryption (mandatory, zero-knowledge)

Key hierarchy (all client-side; server never sees a key or plaintext):

```
passphrase ──Argon2id(salt)──► KEK_pass ─┐
                                          ├─ wrap ─► stored on server (keys.json)
recovery-key (one-time, shown at setup) ─► KEK_rec ┘
                                          │
      random 256-bit Master Data Key (MDK) ◄── unwrap with either KEK
                                          │
   • blob encryption:  XChaCha20-Poly1305(MDK-derived subkey)
   • blob name:        HMAC(MDK, plaintext_hash)   ← dedup w/o content correlation
   • path segments:    HMAC(MDK, system|game|slot) ← server sees opaque dirs only
   • heads.json body:  encrypted with MDK
```

- **Recovery key** avoids "one forgotten passphrase = total loss" while staying
  zero-knowledge. New-device enrollment: enter passphrase **or** recovery key →
  unwrap MDK.
- Crypto crates are `no_std + alloc` compatible (`argon2`, `chacha20poly1305`,
  `sha2`, `hmac`) so encryption lives in the pure core.
- **Consequence (accepted):** because metadata is obfuscated, saves are NOT plain
  inspectable/rclone-able zips on the server — this is the cost of "most private".

---

## 6. Backend layout (dumb WebDAV)

Race-safety by data model (no server logic, no shared-file writes):

```
webdav:/waystone/
  keys.json                         # wrapped MDK (pass + recovery), salts
  devices.json                      # deviceId registry (append-only per device)
  <hmac_sys>/<hmac_game>/<hmac_slot>/
    heads/<deviceId>.json           # each device writes ONLY its own file
    blobs/<hmac(plaintext_hash)>.bin # immutable, encrypted, content-addressed
    history/<ts>-<deviceId>.json     # append-only pointers
```

Every mutable pointer is **per-device** (`heads/<deviceId>.json`); blobs and
history are **immutable/append-only**. No two clients ever write the same path →
no lost-update race on plain WebDAV, no `LOCK` needed.

---

## 7. Sync algorithm (three-way detection, never byte-merge)

Per group, per device:

```
local  = hash of the local entry
merged = fold(all heads/<deviceId>.json)  → current head + set of device heads
base   = heads/<thisDevice>.json           # what this device last agreed to

local == head        → in sync            (write base = local)
local == base        → remote advanced    → PULL head → decrypt → write() to device
head  == base        → local advanced     → PUSH local blob, update this device head
else                 → CONFLICT (both moved since base)
                        default policy: newest mtime wins as head,
                        loser archived in history/ (never deleted).
                        policy is configurable → {newest-wins+archive | prompt}
```

Nothing is ever destroyed (append-only blobs + history) → "restore previous
version" is always possible. This invariant is the trust anchor of the tool.

---

## 8. Adapters (source plugins)

Pure **format** logic in core (parse/emit); the shell supplies bytes + does I/O.

```rust
trait Adapter {
    fn id(&self) -> &str;                 // "jksv" | "mgba" | "twilight"
    fn systems(&self) -> &[SystemId];
    fn capabilities(&self) -> Caps;       // { writable, multiple_slots, emits }
    // shell calls these with in-memory file trees, not filesystem handles:
    fn normalize(&self, raw: RawTree) -> Vec<SaveEntry>;
    fn to_native(&self, entry: &SaveEntry, blob: Bytes) -> RawTree;
}
```

v1 adapters:
- **jksv** — reads/writes JKSV's on-disk backup layout (`JKSV/<Title>/<backup>/…`);
  system = switch/3ds, key = titleId, kind = native.
- **mgba** — one `.sav` battery per ROM; system = gba/gbc/gb, key = ROM
  serial/CRC (or weak filename), kind = battery/savestate.

Later: **twilight** (TWiLight++/nds-bootstrap), **checkpoint** (different layout).

---

## 9. Design system

One token set (typography scale, palette, spacing, radii, motion curves, icon
set) → implemented natively per shell. "Same design DNA, platform-appropriate"
(the Apple multi-device approach), **not** a forced iOS skin.

| Shell | UI framework | Notes |
|---|---|---|
| Android | Jetpack Compose, custom theme (not default Material) | closest to premium/iOS-ish |
| Switch | `borealis` (or custom deko3d) | polished, console-native |
| 3DS | `citro2d`/`citro3d` | clean & on-brand; hardware-bounded (400×240 dual) |
| Desktop | `ratatui` TUI (v1) | tidy utility; GUI later reuses tokens |

Design-system tokens live in `design/tokens.json` — a shared, framework-agnostic
source consumed/ported by each shell.

---

## 10. Phased roadmap

**Milestone 1 — buildable & testable on desktop NOW (no console toolchains/hardware):**
- Cargo workspace scaffold; `core` crate with model, canonical zip, sha256, E2EE,
  conflict/heads, jksv + mgba format adapters — with unit tests + **golden test
  vectors** (input → exact zip bytes → exact hash) that all shells must later pass.
- `desktop` CLI/TUI shell: WebDAV client, exercises the full pipeline end-to-end
  (extract-format → zip → encrypt → hash → push/pull → conflict → decrypt →
  restore) and acts as a "second device" to validate sync.
- `deploy/`: `dufs` + `docker-compose.yml` for Dokploy; setup README.
- Repo hygiene: `README`, `LICENSE` (GPL-3.0), `CONTRIBUTING`, `ARCHITECTURE.md`,
  `design/tokens.json`.

**M2 — Switch shell** (C++/libnx + borealis; save-mount extraction; core via C ABI).
**M3 — Android shell** (Kotlin/Compose; core via UniFFI; SAF + OkHttp).
**M4 — 3DS shell** (C++/libctru + citro2d; core via C ABI).

Every shell targets the same server format + golden vectors, so they interoperate
the moment they land.

---

## 11. Pattern references

- **JKSV** (GPL-3.0) — on-disk backup layout to stay compatible with; save-mount
  reference. `github.com/J-D-K/JKSV`
- **Checkpoint** (GPL-3.0) — 3DS+Switch homebrew save-manager patterns.
- **dufs** — the WebDAV backend container. `github.com/sigoden/dufs`
- **ctru-rs / cargo-3ds**, **aarch64-switch-rs / cargo-nx** — console Rust (if any
  shell code goes Rust); **libnx / libctru** (devkitPro) for the C++ shells.
- **UniFFI** — Rust↔Kotlin bindings for Android.
- **age/rage**, `argon2`, `chacha20poly1305` — E2EE reference.
- **borealis** — Switch homebrew UI library.

---

## 12. Deferred / open

- Network transport ergonomics (FTP fallback, LAN discovery) — v1 is direct WebDAV.
- Multi-user hosted service — explicitly out of scope (self-host per user).
- Desktop GUI — after v1.
- GC/pruning policy for `history/` growth.
