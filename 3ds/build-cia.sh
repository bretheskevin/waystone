#!/usr/bin/env bash
# Build the Waystone 3DS .cia via Docker.
#
# makerom + bannertool are x86_64-only prebuilts, so the CIA packaging step runs
# in an amd64 container (via Rosetta on Apple Silicon). The normal .3dsx build
# stays arm64-native for a fast iteration loop (see 3ds/CLAUDE.md); this script is
# only for producing the installable .cia locally. CI builds it natively on x86_64.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
IMAGE=waystone-3ds-amd64
echo "[build-cia] building amd64 image ($IMAGE) ..."
docker build --platform linux/amd64 -t "$IMAGE" -f 3ds/Dockerfile 3ds/
echo "[build-cia] cross-building FFI + make cia (amd64/Rosetta) ..."
docker run --rm --platform linux/amd64 -v "$REPO_ROOT":/work -w /work "$IMAGE" bash -lc \
  "cargo +nightly build -Zbuild-std=core,alloc --target armv6k-nintendo-3ds --features 3ds -p waystone-ffi --release && make -C 3ds clean && make -C 3ds cia"
echo "[build-cia] done -> 3ds/waystone-3ds-spike.cia"
