# Waystone

Cross-platform game-save sync tool. Back up and share saves across Nintendo
Switch, Android, and 3DS — spanning emulator saves (mGBA, TWiLight++) and
native installed-game saves. Self-hosted backend, mandatory client-side E2EE,
open source.

## Platform & source matrix

| Platform | Native saves | mGBA saves | TWiLight++ saves |
|---|---|---|---|
| Nintendo Switch | ✓ (jksv) | — | — |
| 3DS | ✓ (jksv) | — | Planned (M4) |
| Android | Planned (M3) | ✓ (M3) | — |
| Desktop CLI | ✓ via jksv layout | ✓ | — |

v1 adapters: **jksv** (Switch/3DS native) and **mgba** (GBA/GBC/GB battery saves).

## Build status

| Milestone | Scope | Status |
|---|---|---|
| **M1** | `core` library + `desktop` CLI + `dufs` backend | In progress |
| **M2** | Switch shell (C++/libnx + borealis) | Planned |
| **M3** | Android shell (Kotlin/Compose, core via UniFFI) | Planned |
| **M4** | 3DS shell (C++/libctru + citro2d) | Planned |

M1 is buildable and testable on any desktop (no console hardware needed).

## Quick start

### Self-host the backend (dufs on Dokploy)

```sh
# Deploy dufs WebDAV server via Dokploy (see deploy/README for full setup)
cd deploy && docker compose up -d
```

For a persistent Dokploy deployment: point a new Docker Compose service at
`deploy/docker-compose.yml`. Add a Traefik domain for HTTPS — dufs handles
WebDAV over plain HTTP; Dokploy/Traefik supplies TLS + Let's Encrypt.

### Initialize and sync

```sh
# Build
cargo build --workspace

# Initialize vault (one-time per server)
cargo run -p waystone-desktop -- init --server http://localhost:5000

# Push saves
cargo run -p waystone-desktop -- push --source ~/JKSV --adapter jksv --system switch

# Pull saves on another device
cargo run -p waystone-desktop -- pull --dest ~/JKSV-restore --adapter jksv --system switch
```

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design: data model,
canonical packaging, E2EE key hierarchy, sync algorithm, adapter API, and the
phased roadmap.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
