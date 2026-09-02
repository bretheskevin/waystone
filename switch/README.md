# Waystone — Nintendo Switch Shell

## What this is

This directory contains M2 spikes for the Switch homebrew:

1. **FFI link spike** — `libwaystone_ffi.a` (Rust, no_std) links cleanly into a real
   Switch `.nro`. Full C-ABI round-trip verified (vault init, zip, hash, encrypt,
   decrypt, unzip).

2. **WebDAV networking spike** — switch-curl (libcurl for Switch) compiles and links
   into the same `.nro`. The demo issues PUT, GET, and PROPFIND(Depth:1) against a
   WebDAV server, mirroring the desktop client's verb shapes. Compile+link verified;
   not yet run on hardware.

The final Switch shell (borealis UI + save-mount + WiFi/WebDAV) will replace these
spikes once the foundation is confirmed.

## Verified result

`waystone-spike.nro` links both `libwaystone_ffi.a` and `libcurl` (switch-curl).

- FFI: `ws_vault_init` -> `ws_canonical_zip` / `ws_content_hash` ->
  `ws_vault_encrypt_blob` -> `ws_vault_decrypt_blob` -> `ws_unzip`
- Net: `net_webdav_probe` issues PUT (upload probe blob) -> GET (download and compare)
  -> PROPFIND Depth:1 (list collection), printing HTTP status codes and response
  bodies to the libnx console.

**Compile + link verified. Not yet run on hardware.**

## HTTPS/TLS support

This build uses **verified HTTPS** (`https://`). TLS is enabled via the libnx TLS
backend (Switch system SSL service), with the bundled `romfs:/cacert.pem` (Mozilla
CA bundle, ISRG Root X1 / Let's Encrypt) loaded on top of the system CA store.

`CURLOPT_SSL_VERIFYPEER` and `CURLOPT_SSL_VERIFYHOST` are both enforced — the
connection is rejected if the server certificate cannot be verified.

The save data itself is additionally protected by Waystone's mandatory client-side
E2EE: blobs are ciphertext before upload, and paths are HMAC-obfuscated.

**Known runtime gotcha:** The Switch clock must be set correctly, or certificate
date validation will fail. Make sure the console is connected to the internet and
has synchronized its clock before running.

**Plain HTTP** (`http://`) URLs still work — the TLS options are harmless for
unencrypted connections — but are not recommended as basic-auth credentials are
sent in cleartext.

## Build

### One-liner (recommended)

```sh
./switch/build.sh
```

This runs the `waystone-switch` Docker image (devkitpro/devkita64 + switch-curl +
rustup nightly + rust-src), cross-compiles `libwaystone_ffi.a`, and links the `.nro`.

### Manual Docker steps

```sh
docker build -t waystone-switch ./switch

docker run --rm -v "$PWD":/work -w /work waystone-switch bash -c "
  cargo +nightly build -Zbuild-std=core,alloc \
    --target aarch64-nintendo-switch-freestanding \
    --release --features switch -p waystone-ffi && \
  make -C switch"
```

Output is `switch/waystone-spike.nro`.

### Setting the WebDAV server URL and credentials

The server URL and credentials default to placeholders (`CHANGEME`). To point at a
real server, override them at build time via the Makefile's `DEFINES` variable:

```sh
docker run --rm -v "$PWD":/work -w /work waystone-switch bash -c "
  cargo +nightly build -Zbuild-std=core,alloc \
    --target aarch64-nintendo-switch-freestanding \
    --release --features switch -p waystone-ffi && \
  make -C switch 'DEFINES=-DWAYSTONE_WEBDAV_URL=\\\"https://your.domain\\\" \
    -DWAYSTONE_WEBDAV_USER=\\\"your_user\\\" \
    -DWAYSTONE_WEBDAV_PASS=\\\"your_pass\\\"'"
```

The `DEFINES` variable feeds into `CFLAGS` (see `switch/Makefile` line 29). The
`#ifndef` guards in `main.cpp` skip the placeholder defaults when these `-D` flags
are present.

**Never commit real IPs or credentials.** The defaults are safe placeholders.

## Corporate proxy / Zscaler CA

If your network performs SSL inspection (e.g. Zscaler), the Docker build will fail when
downloading devkitPro or rustup packages over HTTPS.

Fix: drop your corporate CA certificate(s) (`.pem`) into `switch/certs/`. The Dockerfile
injects any certs it finds there into the image's trust store. This directory is gitignored.
