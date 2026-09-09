#!/usr/bin/env bash
# Runs INSIDE the borealis-preview container.
# Captures PNG screenshots for all Waystone wizard screens.
# The borealis demo capture is optional (skipped if demo binary is absent).
#
# Outputs:
#   out/setup-welcome.png — SetupActivity step 0 (Welcome)
#   out/setup-field.png   — SetupActivity step 1 (Server URL, pre-filled)
#   out/unlock.png        — UnlockActivity step 0 (Vault Passphrase)
#   out/recovery.png      — RecoveryKeyActivity (full-screen recovery key)
#   demo.png              — borealis demo (regression check, optional)
set -euo pipefail

DEMO_BIN=/work/switch/lib/borealis/build-preview/borealis_demo
WIZARD_BIN=/work/switch/preview/build-wizard/waystone_preview
BOREALIS_DIR=/work/switch/lib/borealis
DEMO_OUT=/work/switch/preview/demo.png
WIZARD_OUT=/work/switch/preview/out

if [ ! -x "$WIZARD_BIN" ]; then
    echo "ERROR: wizard binary not found at $WIZARD_BIN — run build.sh first" >&2
    exit 1
fi

mkdir -p "$WIZARD_OUT"

export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
# Force dark theme via borealis GLFW platform env var
export BOREALIS_THEME=DARK

# Start virtual framebuffer (1280×720, 24-bit)
Xvfb :99 -screen 0 1280x720x24 -ac &
XVFB_PID=$!
sleep 1
export DISPLAY=:99

echo "Xvfb PID: $XVFB_PID  DISPLAY: $DISPLAY"

# ---------------------------------------------------------------------------
# Helper: run a binary for SETTLE seconds then capture a screenshot.
# Usage: capture <out_path> <run_dir> <binary> [args...]
# ---------------------------------------------------------------------------
capture() {
    local out_path="$1" run_dir="$2" bin="$3"
    shift 3
    local settle=5

    echo "  Running: $bin $* → $out_path"
    cd "$run_dir"
    "$bin" "$@" &
    local pid=$!
    sleep "$settle"
    import -display "$DISPLAY" -window root "$out_path"
    echo "  Screenshot saved: $out_path"
    identify "$out_path" 2>/dev/null || true
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    cd /
}

# ---------------------------------------------------------------------------
# 1. Borealis demo — regression check (optional)
# ---------------------------------------------------------------------------
if [ -x "$DEMO_BIN" ]; then
    echo "=== Capturing borealis demo ==="
    BOREALIS_THEME=LIGHT capture "$DEMO_OUT" "$BOREALIS_DIR" "$DEMO_BIN"
fi

# ---------------------------------------------------------------------------
# 2-5. Waystone wizard screens (dark theme + Waystone tint)
# ---------------------------------------------------------------------------
echo "=== Capturing wizard: setup-welcome ==="
capture "$WIZARD_OUT/setup-welcome.png" "$BOREALIS_DIR" "$WIZARD_BIN" setup-welcome

echo "=== Capturing wizard: setup-field (Server URL step) ==="
capture "$WIZARD_OUT/setup-field.png" "$BOREALIS_DIR" "$WIZARD_BIN" setup-field

echo "=== Capturing wizard: unlock ==="
capture "$WIZARD_OUT/unlock.png" "$BOREALIS_DIR" "$WIZARD_BIN" unlock

echo "=== Capturing wizard: recovery key ==="
capture "$WIZARD_OUT/recovery.png" "$BOREALIS_DIR" "$WIZARD_BIN" recovery

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
kill "$XVFB_PID" 2>/dev/null || true
wait "$XVFB_PID" 2>/dev/null || true

echo ""
echo "=== Screenshots written to $WIZARD_OUT/ ==="
ls -lh "$WIZARD_OUT/"
