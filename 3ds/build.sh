#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE=waystone-3ds

echo "--- Building Docker image ---"
docker build -t "$IMAGE" "$SCRIPT_DIR"

run_docker() {
    docker run --rm -v "$REPO_ROOT":/work -w /work "$IMAGE" bash -lc "$1"
}

CARGO_BASE="cargo +nightly build -Zbuild-std=core,alloc --features 3ds -p waystone-ffi --release"
CARGO_A="$CARGO_BASE --target armv6k-nintendo-3ds"
CARGO_B="$CARGO_BASE --target ./3ds/armv6k-none-eabi.json"
MAKE_A="make -C 3ds"
MAKE_B="make -C 3ds WAYSTONE_TARGET=armv6k-none-eabi"

echo ""
echo "--- Attempt A: built-in armv6k-nintendo-3ds ---"
if run_docker "$CARGO_A && $MAKE_A"; then
    DSX="$SCRIPT_DIR/waystone-3ds-spike.3dsx"
    echo ""
    echo "SUCCESS (built-in target)"
    ls -lh "$DSX" 2>/dev/null || echo "(3dsx at $DSX)"
    exit 0
fi

echo ""
echo "--- Attempt A failed. Cleaning build artifacts ---"
run_docker "make -C 3ds clean" || true

echo ""
echo "--- Attempt B: custom armv6k-none-eabi.json ---"
if run_docker "$CARGO_B && $MAKE_B"; then
    DSX="$SCRIPT_DIR/waystone-3ds-spike.3dsx"
    echo ""
    echo "SUCCESS (custom target)"
    ls -lh "$DSX" 2>/dev/null || echo "(3dsx at $DSX)"
    exit 0
fi

echo ""
echo "=== BOTH TARGETS FAILED ==="
echo "Review the linker errors above."
echo "Attempt A: built-in target -- likely ABI/relocation mismatch"
echo "Attempt B: custom target   -- likely missing symbols or incompatible object format"
exit 1
