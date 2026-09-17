#!/usr/bin/env bash
# Fast screenshot: captures a single preview mode using the persistent container.
# Usage: bash switch/preview/dev-shot.sh [mode]   (default: dashboard)
#
# The container must already be running (run dev-build.sh first).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CONTAINER=waystone-preview-dev
MODE="${1:-dashboard}"

if ! docker container inspect "$CONTAINER" >/dev/null 2>&1; then
    echo "ERROR: container '$CONTAINER' not found — run dev-build.sh first." >&2
    exit 1
fi

if [ "$(docker container inspect -f '{{.State.Status}}' "$CONTAINER")" != "running" ]; then
    echo "=== Starting container $CONTAINER ==="
    docker start "$CONTAINER"
fi

echo "=== Capturing mode '$MODE' ==="
docker exec \
    -e CAPTURE_ONLY="$MODE" \
    "$CONTAINER" \
    bash /work/switch/preview/run-headless-inner.sh

echo ""
echo "=== Screenshot: $REPO_ROOT/switch/preview/out/${MODE}.png ==="
