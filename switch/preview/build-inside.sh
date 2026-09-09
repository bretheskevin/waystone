#!/usr/bin/env bash
# Runs INSIDE the borealis-preview container.
# 1. Compiles filestream stubs (for features_cpu.c).
# 2. Builds the borealis demo with meson/ninja (proves borealis compiles OK).
# 3. Builds the Waystone wizard preview binary via build-wizard-inside.sh.
set -euo pipefail

# Compile missing libretro-common filestream symbols into a stub archive.
# The .o is also reused by build-wizard-inside.sh.
gcc -c /work/switch/preview/stubs/filestream.c -o /tmp/filestream_stubs.o
ar rcs /tmp/libfilestream_stubs.a /tmp/filestream_stubs.o

cd /work/switch/lib/borealis
rm -rf build-preview

# Two fixes for the standard glibc toolchain:
#   -include cstdint   — platform.hpp uses uint32_t without including <cstdint>
#   LDFLAGS stub lib   — features_cpu.c calls filestream_* not shipped in the meson.build subset
LDFLAGS="/tmp/libfilestream_stubs.a" \
meson setup build-preview \
    --buildtype=release \
    --warnlevel=0 \
    -Dcpp_args="-include cstdint"

ninja -C build-preview

echo "Binary: /work/switch/lib/borealis/build-preview/borealis_demo ($(du -sh build-preview/borealis_demo | cut -f1))"

# Build the Waystone wizard preview binary (fresh compilation of borealis + our UI).
bash /work/switch/preview/build-wizard-inside.sh
