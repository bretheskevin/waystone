#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE=waystone-switch

echo "--- Building Docker image ---"
docker build -t "$IMAGE" "$SCRIPT_DIR"

run_docker() {
    docker run --rm -v "$REPO_ROOT":/work -w /work "$IMAGE" bash -lc "$1"
}

CARGO_BASE="cargo +nightly build -Zbuild-std=core,alloc --features switch -p waystone-ffi --release"
CARGO_A="$CARGO_BASE --target aarch64-nintendo-switch-freestanding"
CARGO_B="$CARGO_BASE --target ./switch/aarch64-none-elf.json"
MAKE_A="make -C switch"
MAKE_B="make -C switch WAYSTONE_TARGET=aarch64-none-elf"

echo ""
echo "--- Attempt A: built-in aarch64-nintendo-switch-freestanding ---"
if run_docker "$CARGO_A && $MAKE_A"; then
    NRO="$SCRIPT_DIR/waystone.nro"
    echo ""
    echo "SUCCESS (built-in target)"
    ls -lh "$NRO"
    exit 0
fi

echo ""
echo "--- Attempt A failed. Cleaning build artifacts ---"
run_docker "make -C switch clean" || true

echo ""
echo "--- Attempt B: custom aarch64-none-elf.json ---"
if run_docker "$CARGO_B && $MAKE_B"; then
    NRO="$SCRIPT_DIR/waystone.nro"
    echo ""
    echo "SUCCESS (custom target)"
    ls -lh "$NRO"
    exit 0
fi

echo ""
echo "=== BOTH TARGETS FAILED ==="
echo "Review the linker errors above."
echo "Attempt A: built-in target -- likely ABI/relocation/TLS mismatch"
echo "Attempt B: custom target   -- likely missing symbols or incompatible object format"
echo ""
echo "Document the exact linker errors in switch/README.md and ROADMAP.md."
exit 1
