#!/bin/sh
set -eu
if [ -n "${WAYSTONE_AUTH:-}" ]; then
  # Production: enforce auth, do NOT allow anonymous.
  # --allow-all enables full CRUD; -a enforces authentication (anonymous gets 401).
  exec /bin/dufs /data --enable-cors --allow-all -a "$WAYSTONE_AUTH"
else
  # Local/dev: anonymous full access.
  exec /bin/dufs /data --enable-cors --allow-all
fi
