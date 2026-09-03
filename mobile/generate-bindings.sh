#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cargo build -p waystone-mobile

if [[ "$(uname)" == "Darwin" ]]; then
    CDYLIB="$WORKSPACE_ROOT/target/debug/libwaystone_mobile.dylib"
else
    CDYLIB="$WORKSPACE_ROOT/target/debug/libwaystone_mobile.so"
fi

if [[ ! -f "$CDYLIB" ]]; then
    echo "ERROR: cdylib not found at $CDYLIB" >&2
    exit 1
fi

cargo run -p waystone-mobile --bin uniffi-bindgen -- generate \
    --library "$CDYLIB" \
    --language kotlin \
    --out-dir "$SCRIPT_DIR/bindings" \
    --no-format

echo "Kotlin bindings generated at $SCRIPT_DIR/bindings/"
