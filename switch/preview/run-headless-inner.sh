#!/usr/bin/env bash
# Runs INSIDE the borealis-preview container.
# Captures PNG screenshots for all Waystone wizard screens.
# The borealis demo capture is optional (skipped if demo binary is absent).
#
# Outputs:
#   out/setup-welcome.png  — SetupActivity step 0 (Welcome)
#   out/setup-field.png    — SetupActivity step 1 (Server URL, pre-filled)
#   out/creating-vault.png — SetupActivity step 3, "Creating vault…" progress state
#   out/unlock.png         — UnlockActivity step 0 (Vault Passphrase)
#   out/recovery.png       — RecoveryKeyActivity (full-screen recovery key)
#   out/no-internet.png    — NoInternetActivity (no connection screen)
#   demo.png               — borealis demo (regression check, optional)
set -euo pipefail

DEMO_BIN=/work/switch/lib/borealis/build-preview/borealis_demo
WIZARD_BIN=/work/switch/preview/build-wizard/waystone_preview
BOREALIS_DIR=/work/switch/lib/borealis
# Wizard runs from here so ./resources/ resolves to switch/preview/resources/ (preview-only).
PREVIEW_DIR=/work/switch/preview
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
# Binary runs from PREVIEW_DIR so ./resources/ → switch/preview/resources/
# (preview-only; never the shared borealis resources that feed the .nro).
# ---------------------------------------------------------------------------
echo "=== Capturing wizard: setup-welcome ==="
capture "$WIZARD_OUT/setup-welcome.png" "$PREVIEW_DIR" "$WIZARD_BIN" setup-welcome

# ---------------------------------------------------------------------------
# 6. Transition filmstrip + GIF
#    The binary auto-advances from step 0→1 after 4 s (with PREVIEW_SLOW_TRANSITION
#    the animation lasts 2 s, giving time for mid-animation frame capture).
#    Capture 6 frames: before, ~0%, ~20%, ~40%, ~60%, ~80%, then assemble GIF.
# ---------------------------------------------------------------------------
echo "=== Capturing transition filmstrip ==="
mkdir -p "$WIZARD_OUT"
cd "$PREVIEW_DIR"
"$WIZARD_BIN" transition &
TRANS_PID=$!

# Wait for borealis to initialise (~2 s) then stay at welcome step until t=4s timer fires
sleep 3
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f0.png"  # before (welcome)
echo "  frame 0 (before)"

# Timer fires at ~4s from binary start → ~5s from script start of this section
# Wait until ~5s → animation starts, capture mid-animation frames
sleep 2
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f1.png"  # ~0%
echo "  frame 1 (~0%)"
sleep 0.4
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f2.png"  # ~20%
echo "  frame 2 (~20%)"
sleep 0.4
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f3.png"  # ~40%
echo "  frame 3 (~40%)"
sleep 0.4
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f4.png"  # ~60%
echo "  frame 4 (~60%)"
sleep 0.4
import -display "$DISPLAY" -window root "$WIZARD_OUT/transition-f5.png"  # ~80%+
echo "  frame 5 (~80%+)"

kill "$TRANS_PID" 2>/dev/null || true
wait "$TRANS_PID" 2>/dev/null || true
cd /

# Assemble filmstrip frames into an animated GIF (40 centiseconds = 400 ms per frame)
if command -v convert >/dev/null 2>&1; then
    convert -delay 40 -loop 0 \
        "$WIZARD_OUT/transition-f0.png" \
        "$WIZARD_OUT/transition-f1.png" \
        "$WIZARD_OUT/transition-f2.png" \
        "$WIZARD_OUT/transition-f3.png" \
        "$WIZARD_OUT/transition-f4.png" \
        "$WIZARD_OUT/transition-f5.png" \
        "$WIZARD_OUT/transition.gif"
    echo "  GIF assembled: $WIZARD_OUT/transition.gif"
else
    echo "  WARNING: convert not found — only filmstrip PNGs produced" >&2
fi

echo "=== Capturing wizard: setup-field (Server URL step) ==="
capture "$WIZARD_OUT/setup-field.png" "$PREVIEW_DIR" "$WIZARD_BIN" setup-field

echo "=== Capturing wizard: creating-vault (progress state) ==="
capture "$WIZARD_OUT/creating-vault.png" "$PREVIEW_DIR" "$WIZARD_BIN" creating-vault

echo "=== Capturing wizard: unlock ==="
capture "$WIZARD_OUT/unlock.png" "$PREVIEW_DIR" "$WIZARD_BIN" unlock

echo "=== Capturing wizard: recovery key ==="
capture "$WIZARD_OUT/recovery.png" "$PREVIEW_DIR" "$WIZARD_BIN" recovery

echo "=== Capturing wizard: no-internet ==="
capture "$WIZARD_OUT/no-internet.png" "$PREVIEW_DIR" "$WIZARD_BIN" no-internet

echo "=== Capturing conflicts: normal inbox ==="
capture "$WIZARD_OUT/conflicts.png" "$PREVIEW_DIR" "$WIZARD_BIN" conflicts

echo "=== Capturing conflicts: confirm banner ==="
# conflicts-confirm mode auto-triggers the banner after 1 second.
# The capture() helper waits 5 seconds, so the banner is visible.
capture "$WIZARD_OUT/conflicts-confirm.png" "$PREVIEW_DIR" "$WIZARD_BIN" conflicts-confirm

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
kill "$XVFB_PID" 2>/dev/null || true
wait "$XVFB_PID" 2>/dev/null || true

echo ""
echo "=== Screenshots written to $WIZARD_OUT/ ==="
ls -lh "$WIZARD_OUT/"
