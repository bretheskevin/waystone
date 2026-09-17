#!/usr/bin/env bash
# Fast incremental build using a persistent container — no docker build, no full recompile.
# Only changed source files are recompiled; borealis objects are reused from the mounted volume.
#
# Prerequisites: run 'bash switch/preview/build.sh' once to create the borealis-preview image.
# Usage: bash switch/preview/dev-build.sh [PREVIEW_FORCE_REBUILD=1]
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE=borealis-preview
CONTAINER=waystone-preview-dev

# Ensure the image exists; if not, print a one-line hint and exit.
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "ERROR: image '$IMAGE' not found — run 'bash switch/preview/build.sh' once to build it." >&2
    exit 1
fi

# Create persistent container if absent, start it if stopped.
if ! docker container inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "=== Creating persistent container $CONTAINER ==="
    docker run -d \
        --name "$CONTAINER" \
        -v "$REPO_ROOT":/work \
        "$IMAGE" \
        tail -f /dev/null
elif [ "$(docker container inspect -f '{{.State.Status}}' "$CONTAINER")" != "running" ]; then
    echo "=== Starting existing container $CONTAINER ==="
    docker start "$CONTAINER"
fi

echo "=== Incremental build in $CONTAINER ==="
docker exec \
    -e PREVIEW_FORCE_REBUILD="${PREVIEW_FORCE_REBUILD:-0}" \
    "$CONTAINER" \
    bash /work/switch/preview/build-wizard-inside.sh

echo ""
echo "=== Binary: $REPO_ROOT/switch/preview/build-wizard/waystone_preview ==="
