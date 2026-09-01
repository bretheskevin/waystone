# Waystone - Deployment

## Quick start (local development)

```sh
cd deploy
docker compose up -d
```

This starts `dufs` on `http://localhost:5000` with no authentication.

## Production (Dokploy + Traefik)

1. Set environment variables:
   - `WAYSTONE_DOMAIN` - your domain (e.g., `saves.example.com`)
   - `WAYSTONE_AUTH` - dufs auth string (e.g., `user:password@/:rw`)
   - `WAYSTONE_PORT` - host port (default: 5000)

2. Deploy via Dokploy or `docker compose up -d`.

3. Traefik labels handle HTTPS via Let's Encrypt automatically.

## Data

Save data is stored in the `waystone-data` Docker volume.
All data on the server is encrypted (E2EE); the server never sees plaintext.

## Backup

```sh
docker run --rm -v waystone-data:/data -v $(pwd):/backup alpine tar czf /backup/waystone-backup.tar.gz /data
```
