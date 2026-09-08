# Waystone - Deployment

## Quick start (local development)

```sh
cd deploy
docker compose up -d --build
```

This starts `dufs` on `http://localhost:5005` with no authentication (anonymous read/write).

The first run builds a tiny local image (adds the auth-gating entrypoint on top of `sigoden/dufs`).
After the first build, `--build` is a no-op unless `deploy/` files change.

## Production (Dokploy + Traefik)

Set these environment variables in the Dokploy UI (or export them on the host):

| Variable | Required | Example | Purpose |
|----------|----------|---------|---------| 
| `WAYSTONE_AUTH` | **Yes** | `user:password@/:rw` | dufs auth string -- enables HTTP Basic auth and drops anonymous access. |
| `WAYSTONE_DOMAIN` | Yes | `saves.example.com` | Traefik host rule + Let's Encrypt HTTPS. |
| `WAYSTONE_PORT` | No | `5005` (default) | Host port mapped to dufs. |

Then deploy via Dokploy or:

```sh
WAYSTONE_AUTH='user:password@/:rw' WAYSTONE_DOMAIN=saves.example.com docker compose up -d --build
```

> **Security:** Omitting `WAYSTONE_AUTH` leaves the server **world-writable** (anonymous mode).
> This is fine for local development but **must not** be used in production.
> Data on the server is E2EE (the server never sees plaintext), but without auth anyone can
> write, delete, or bloat the store.

The Waystone client uses `https://<domain>` as the WebDAV URL, with the username and password
from `WAYSTONE_AUTH` (the part before `@`).

## How auth gating works

`deploy/entrypoint.sh` checks `WAYSTONE_AUTH` at container start:

- **Set** (non-empty): dufs starts with `--allow-all -a "$WAYSTONE_AUTH"` (all operations enabled, auth enforced -- anonymous gets 401).
- **Unset or empty**: dufs starts with `--allow-all` only (anonymous read/write, local default).

This means there is no separate "auth mode" toggle -- the presence of a credential string IS the toggle.

## Data

Save data is stored in the `waystone-data` Docker volume.
All data on the server is encrypted (E2EE); the server never sees plaintext.

## Backup

```sh
docker run --rm -v waystone-data:/data -v $(pwd):/backup alpine tar czf /backup/waystone-backup.tar.gz /data
```
