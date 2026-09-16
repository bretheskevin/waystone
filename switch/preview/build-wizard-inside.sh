#!/usr/bin/env bash
# Compile the Waystone wizard preview binary INSIDE the borealis-preview container.
# Called from build-inside.sh after the borealis demo build has run,
# but also works standalone (compiles filestream_stubs.o if missing).
#
# This script compiles borealis from scratch plus our wizard UI + stubs and
# links them into a single waystone_preview binary.
set -euo pipefail

BOREALIS=/work/switch/lib/borealis/library
PREVIEW=/work/switch/preview
UI=/work/switch/source/ui
SHELL_COMMON=/work/shell-common
SWITCH_SRC=/work/switch/source
FFI_INCLUDE=/work/ffi/include

OUT=/work/switch/preview/build-wizard
mkdir -p "$OUT"

# Compile filestream stubs if not already built (build-inside.sh does this
# first, but this script may also be invoked standalone for the wizard only).
if [ ! -f /tmp/filestream_stubs.o ]; then
    echo "  [CC ] filestream_stubs (standalone)" >&2
    gcc -c /work/switch/preview/stubs/filestream.c -o /tmp/filestream_stubs.o
fi

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
INCLUDES=(
    # Borealis public headers
    "-I${BOREALIS}/include"
    "-I${BOREALIS}/lib/extern/fmt/include"
    "-I${BOREALIS}/include/borealis/extern"
    "-I${BOREALIS}/include/borealis/extern/tinyxml2"
    "-I${BOREALIS}/include/borealis/extern/nanovg-gl"
    "-I${BOREALIS}/lib/extern/yoga/src"
    "-I${BOREALIS}/lib/extern/tweeny/include"
    # Shim directory FIRST so shims/switch.h shadows the absent system <switch.h>
    "-I${PREVIEW}/shims"
    # Engine / shell-common headers (net.h, wsconfig.h, secure_clear.h)
    "-I${SHELL_COMMON}"
    # Engine saves.h (which includes <switch.h> — resolved by the shim above)
    "-I${SWITCH_SRC}"
    # Our wizard UI headers
    "-I${UI}"
    # Waystone FFI header (waystone.h — ws_* types needed by unlock_activity.cpp)
    "-I${FFI_INCLUDE}"
)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------
CXXFLAGS=(
    -std=c++17
    # platform.hpp uses uint32_t without including <cstdint> on some paths
    -include cstdint
    -DYG_ENABLE_EVENTS
    -D__GLFW__
    "-DBRLS_RESOURCES=\"./resources/\""
    -DPREVIEW_SLOW_TRANSITION
    -O2
    # Silence all warnings — vendored borealis code is noisy
    -w
)

CFLAGS=(
    -O2
    -w
)

# ---------------------------------------------------------------------------
# Helper: compile one C++ file → object, echo the object path
# ---------------------------------------------------------------------------
cxx() {
    local src="$1" tag="$2"
    local obj="$OUT/${tag}.o"
    echo "  [CXX] ${tag}" >&2
    g++ "${CXXFLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$obj"
    echo "$obj"
}

# Helper: compile one C file → object, echo the object path
cc() {
    local src="$1" tag="$2"
    local obj="$OUT/${tag}.o"
    echo "  [CC ] ${tag}" >&2
    gcc ${CFLAGS[@]} "${INCLUDES[@]}" -c "$src" -o "$obj"
    echo "$obj"
}

# ---------------------------------------------------------------------------
# Borealis library sources
# ---------------------------------------------------------------------------
echo "=== Compiling borealis library ===" >&2
BRL="$BOREALIS"
OBJS=()

# Core
OBJS+=( $(cxx "$BRL/lib/core/logger.cpp"       brls_logger) )
OBJS+=( $(cxx "$BRL/lib/core/application.cpp"  brls_application) )
OBJS+=( $(cxx "$BRL/lib/core/i18n.cpp"         brls_i18n) )
OBJS+=( $(cxx "$BRL/lib/core/theme.cpp"        brls_theme) )
OBJS+=( $(cxx "$BRL/lib/core/style.cpp"        brls_style) )
OBJS+=( $(cxx "$BRL/lib/core/activity.cpp"     brls_activity) )
OBJS+=( $(cxx "$BRL/lib/core/platform.cpp"     brls_platform) )
OBJS+=( $(cxx "$BRL/lib/core/font.cpp"         brls_font) )
OBJS+=( $(cxx "$BRL/lib/core/util.cpp"         brls_util) )
OBJS+=( $(cxx "$BRL/lib/core/time.cpp"         brls_time) )
OBJS+=( $(cxx "$BRL/lib/core/timer.cpp"        brls_timer) )
OBJS+=( $(cxx "$BRL/lib/core/animation.cpp"    brls_animation) )
OBJS+=( $(cxx "$BRL/lib/core/task.cpp"         brls_task) )
OBJS+=( $(cxx "$BRL/lib/core/view.cpp"         brls_view) )
OBJS+=( $(cxx "$BRL/lib/core/box.cpp"          brls_box) )
OBJS+=( $(cxx "$BRL/lib/core/bind.cpp"         brls_bind) )

# GLFW platform
OBJS+=( $(cxx "$BRL/lib/platforms/glfw/glfw_platform.cpp" brls_glfw_platform) )
OBJS+=( $(cxx "$BRL/lib/platforms/glfw/glfw_video.cpp"    brls_glfw_video) )
OBJS+=( $(cxx "$BRL/lib/platforms/glfw/glfw_input.cpp"    brls_glfw_input) )
OBJS+=( $(cxx "$BRL/lib/platforms/glfw/glfw_font.cpp"     brls_glfw_font) )

# Switch swkbd — compiles fine on Linux (reads stdin on non-Switch path)
OBJS+=( $(cxx "$BRL/lib/platforms/switch/swkbd.cpp" brls_swkbd) )

# Views
OBJS+=( $(cxx "$BRL/lib/views/scrolling_frame.cpp" brls_scrolling_frame) )
OBJS+=( $(cxx "$BRL/lib/views/applet_frame.cpp"    brls_applet_frame) )
OBJS+=( $(cxx "$BRL/lib/views/tab_frame.cpp"       brls_tab_frame) )
OBJS+=( $(cxx "$BRL/lib/views/rectangle.cpp"       brls_rectangle) )
OBJS+=( $(cxx "$BRL/lib/views/sidebar.cpp"         brls_sidebar) )
OBJS+=( $(cxx "$BRL/lib/views/label.cpp"           brls_label) )
OBJS+=( $(cxx "$BRL/lib/views/button.cpp"          brls_button) )
OBJS+=( $(cxx "$BRL/lib/views/image.cpp"           brls_image) )
OBJS+=( $(cxx "$BRL/lib/views/header.cpp"          brls_header) )

# Extern (vendored) — use plain gcc for .c files to avoid C++ incompatibilities
OBJS+=( $(cc  "$BRL/lib/extern/glad/glad.c"                                             brls_glad) )
OBJS+=( $(cc  "$BRL/lib/extern/nanovg-gl/nanovg.c"                                     brls_nanovg) )
OBJS+=( $(cxx "$BRL/lib/extern/tinyxml2/tinyxml2.cpp"                                  brls_tinyxml2) )
OBJS+=( $(cxx "$BRL/lib/extern/fmt/src/format.cc"                                      brls_fmt_format) )
OBJS+=( $(cxx "$BRL/lib/extern/fmt/src/os.cc"                                          brls_fmt_os) )
OBJS+=( $(cc  "$BRL/lib/extern/libretro-common/compat/compat_strl.c"                   brls_compat_strl) )
OBJS+=( $(cc  "$BRL/lib/extern/libretro-common/features/features_cpu.c"                brls_features_cpu) )
OBJS+=( $(cc  "$BRL/lib/extern/libretro-common/encodings/encoding_utf.c"               brls_encoding_utf) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/event/event.cpp"                          brls_yoga_event) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/log.cpp"                                  brls_yoga_log) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/Utils.cpp"                                brls_yoga_utils) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGConfig.cpp"                             brls_yoga_config) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGEnums.cpp"                              brls_yoga_enums) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGLayout.cpp"                             brls_yoga_layout) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGNode.cpp"                               brls_yoga_node) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGStyle.cpp"                              brls_yoga_style) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/YGValue.cpp"                              brls_yoga_value) )
OBJS+=( $(cxx "$BRL/lib/extern/yoga/src/yoga/Yoga.cpp"                                 brls_yoga) )

# ---------------------------------------------------------------------------
# Waystone wizard UI files (compiled read-only from switch/source/ui/)
# ---------------------------------------------------------------------------
echo "=== Compiling wizard UI ===" >&2
OBJS+=( $(cxx "$UI/wizard.cpp"                  ws_wizard) )
OBJS+=( $(cxx "$UI/wizard_activity.cpp"         ws_wizard_activity) )
OBJS+=( $(cxx "$UI/setup_activity.cpp"          ws_setup_activity) )
OBJS+=( $(cxx "$UI/unlock_activity.cpp"         ws_unlock_activity) )
OBJS+=( $(cxx "$UI/recovery_key_activity.cpp"   ws_recovery_key_activity) )
OBJS+=( $(cxx "$UI/no_internet_activity.cpp"    ws_no_internet_activity) )
OBJS+=( $(cxx "$UI/loading_activity.cpp"        ws_loading_activity) )
OBJS+=( $(cxx "$UI/conflicts_activity.cpp"      ws_conflicts_activity) )
OBJS+=( $(cxx "$UI/history_activity.cpp"        ws_history_activity) )
OBJS+=( $(cxx "$UI/theme_tint.cpp"              ws_theme_tint) )

# ---------------------------------------------------------------------------
# Preview stubs (replace Switch-only or complex real implementations)
# ---------------------------------------------------------------------------
echo "=== Compiling preview stubs ===" >&2
OBJS+=( $(cxx "$PREVIEW/stubs/swkbd_stub.cpp"           stub_swkbd) )
OBJS+=( $(cxx "$PREVIEW/stubs/vault_helpers_stub.cpp"   stub_vault_helpers) )
OBJS+=( $(cxx "$PREVIEW/stubs/ws_ffi_stub.cpp"          stub_ws_ffi) )
OBJS+=( $(cxx "$PREVIEW/stubs/saves_stub.cpp"           stub_saves) )
OBJS+=( $(cxx "$PREVIEW/stubs/wsconfig_stub.cpp"        stub_wsconfig) )
OBJS+=( $(cxx "$PREVIEW/stubs/net_status_stub.cpp"      stub_net_status) )
OBJS+=( $(cxx "$PREVIEW/stubs/session_store_stub.cpp"   stub_session_store) )
OBJS+=( $(cxx "$PREVIEW/stubs/conflict_controller_stub.cpp" stub_conflict_controller) )
OBJS+=( $(cxx "$PREVIEW/stubs/history_controller_stub.cpp"  stub_history_controller) )
OBJS+=( $(cxx "$PREVIEW/stubs/sync_controller_stub.cpp"     stub_sync_controller) )
OBJS+=( $(cxx "$PREVIEW/stubs/title_list_activity_stub.cpp" stub_title_list_activity) )

# Preview entry point
OBJS+=( $(cxx "$PREVIEW/main.cpp" preview_main) )

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
echo "=== Linking ===" >&2
GLFW_LIBS=$(pkg-config --libs glfw3 2>/dev/null || echo "-lglfw -ldl")
g++ "${OBJS[@]}" \
    /tmp/filestream_stubs.o \
    $GLFW_LIBS \
    -lGL -lm -lpthread \
    -o "$OUT/waystone_preview"

echo "=== Wizard preview binary: $OUT/waystone_preview ($(du -sh "$OUT/waystone_preview" | cut -f1)) ==="

# ---------------------------------------------------------------------------
# Assemble a preview-only resources directory so borealis GLFW finds the fonts
# WITHOUT polluting switch/lib/borealis/resources/ (which feeds the shipped .nro).
#
# Strategy: copy borealis's own resources into switch/preview/resources/, then
# add the two preview-only fonts there.  run-headless-inner.sh runs the wizard
# binary from switch/preview/ so BRLS_RESOURCES="./resources/" resolves to
# switch/preview/resources/ and never touches switch/lib/borealis/resources/
#
# Generator: switch/preview/fonts/gen_from_svg.py (SVGs from Figma community pack, preview-only)
# ---------------------------------------------------------------------------
PREVIEW_RESOURCES=/work/switch/preview/resources

echo "=== Assembling preview resources dir: $PREVIEW_RESOURCES ==="
mkdir -p "$PREVIEW_RESOURCES"
# Seed with borealis resources (themes, shaders, inter fonts, material icons, …)
cp -rT /work/switch/lib/borealis/resources/ "$PREVIEW_RESOURCES/"

# Install the preview controller-glyph font (NintendoExt PUA U+E0A0-U+E0B6).
FONT_SRC=/work/switch/preview/fonts/User-Switch-Icons.ttf
if [ -f "$FONT_SRC" ]; then
    cp "$FONT_SRC" "$PREVIEW_RESOURCES/User-Switch-Icons.ttf"
    echo "=== Installed preview glyph font into preview resources ==="
else
    echo "WARNING: $FONT_SRC not found -- button glyphs will not render in preview" >&2
fi

# Install Inter-Switch with NintendoExt PUA range stripped as User-Regular.ttf.
# Without this, FONT_REGULAR (Inter-Switch.ttf) would render Inter's own PUA
# glyphs at those codepoints BEFORE the FONT_SWITCH_ICONS fallback fires.
REG_SRC=/work/switch/preview/fonts/User-Regular.ttf
if [ -f "$REG_SRC" ]; then
    cp "$REG_SRC" "$PREVIEW_RESOURCES/User-Regular.ttf"
    echo "=== Installed stripped Inter (User-Regular.ttf) into preview resources ==="
else
    echo "WARNING: $REG_SRC not found -- button glyph fallback will not work" >&2
fi
