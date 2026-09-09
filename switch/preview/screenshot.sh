#!/usr/bin/env bash
# Render all Waystone wizard preview screenshots to switch/preview/out/.
# Also recaptures switch/preview/demo.png (borealis regression check).
# Requires 'bash switch/preview/build.sh' to have been run first.
# Run from repo root: bash switch/preview/screenshot.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IMAGE=borealis-preview

echo "=== Launching headless screenshot session ==="
docker run --rm \
    -v "$REPO_ROOT":/work \
    -e LIBGL_ALWAYS_SOFTWARE=1 \
    -e GALLIUM_DRIVER=llvmpipe \
    -e BOREALIS_THEME=DARK \
    "$IMAGE" \
    bash /work/switch/preview/run-headless-inner.sh

echo ""
echo "=== Done ==="
echo "demo.png:              $REPO_ROOT/switch/preview/demo.png"
echo "setup-welcome.png:     $REPO_ROOT/switch/preview/out/setup-welcome.png"
echo "setup-field.png:       $REPO_ROOT/switch/preview/out/setup-field.png"
echo "unlock.png:            $REPO_ROOT/switch/preview/out/unlock.png"
echo "recovery.png:          $REPO_ROOT/switch/preview/out/recovery.png"
echo "transition.gif:        $REPO_ROOT/switch/preview/out/transition.gif"
echo "transition-f{0-5}.png: $REPO_ROOT/switch/preview/out/transition-f*.png"
