# Waystone — Nintendo Switch Shell

## What this is

This directory contains the M2 FFI link spike: a minimal devkitPro/libnx C++ homebrew
(`switch/source/main.cpp`) whose sole purpose is to prove that `libwaystone_ffi.a` links
cleanly into a real Switch `.nro`. It is a de-risk exercise, not the final app.

The final Switch shell (borealis UI + save-mount + WiFi→WebDAV) will live here once
the link foundation is confirmed. It is.

## Verified result

`libwaystone_ffi.a` (Rust, release, `no_std + alloc`, `aarch64-nintendo-switch-freestanding`
built-in Tier-3 target, `switch` feature) links cleanly into a devkitPro/libnx C++ homebrew
with no custom-target fallback. Output: `switch/waystone-spike.nro` (~215 KB).

The demo drives a full C-ABI round-trip:
`ws_vault_init` → `ws_canonical_zip` / `ws_content_hash` → `ws_vault_encrypt_blob`
→ `ws_vault_decrypt_blob` → `ws_unzip`, printing results to the libnx console.
`nx_getrandom` is wired to libnx entropy (`randomGet`).

**Compile + link verified. Not yet run on hardware.** Runtime verification on real
Switch hardware is the remaining open item for M2.

## Build

### One-liner (recommended)

```sh
./switch/build.sh
```

This runs the `waystone-switch` Docker image (devkitpro/devkita64 + rustup nightly +
rust-src), cross-compiles `libwaystone_ffi.a`, and links the `.nro`.

### Manual Docker steps

```sh
# Build the Docker image (once)
docker build -t waystone-switch ./switch

# Cross-compile the Rust FFI lib and link the .nro
docker run --rm -v "$PWD":/work -w /work waystone-switch bash -c "
  cargo +nightly build -Zbuild-std=core,alloc \
    --target aarch64-nintendo-switch-freestanding \
    --release --features switch -p waystone-ffi && \
  make -C switch"
```

Output is `switch/waystone-spike.nro`.

## Corporate proxy / Zscaler CA

If your network performs SSL inspection (e.g. Zscaler), the Docker build will fail when
downloading devkitPro or rustup packages over HTTPS.

Fix: drop your corporate CA certificate(s) (`.pem`) into `switch/certs/`. The Dockerfile
injects any certs it finds there into the image's trust store. This directory is gitignored
— do not commit certificates.

Contributors on a normal (non-intercepting) network need no certs and can ignore this.
