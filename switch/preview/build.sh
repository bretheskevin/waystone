#!/usr/bin/env bash
# Build the borealis demo AND the Waystone wizard preview binary inside Docker.
# Run from repo root: bash switch/preview/build.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE=borealis-preview

echo "=== Building Docker image ==="
docker build -t "$IMAGE" "$REPO_ROOT/switch/preview"

echo "=== Building borealis demo + Waystone wizard preview ==="
docker run --rm \
    -v "$REPO_ROOT":/work \
    "$IMAGE" \
    bash /work/switch/preview/build-inside.sh

echo "=== Build complete ==="
echo "Run 'bash switch/preview/screenshot.sh' to capture all wizard screenshots."
