FROM devkitpro/devkitarm:latest

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends curl ca-certificates unzip \
    && rm -rf /var/lib/apt/lists/*

# Optional: inject local CA certs (gitignored 3ds/certs/*.pem) so HTTPS works
# behind an SSL-inspecting proxy (e.g. Zscaler). No-op when no certs are present.
COPY certs/ /tmp/extra-certs/
RUN set -eux; \
    for f in /tmp/extra-certs/*.pem /tmp/extra-certs/*.crt; do \
        [ -e "$f" ] || continue; \
        cat "$f" >> /etc/ssl/certs/ca-certificates.crt; \
        if [ -f /opt/devkitpro/pacman/etc/ssl/certs/cacert.pem ]; then \
            cat "$f" >> /opt/devkitpro/pacman/etc/ssl/certs/cacert.pem; \
        fi; \
    done
ENV SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt \
    CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt

# 3ds-curl, 3ds-mbedtls, and libctru all ship preinstalled in the devkitarm
# base image — no dkp-pacman install/upgrade needed.

# Nightly toolchain + rust-src (for -Zbuild-std). Fail loudly on a broken download.
RUN set -eux; \
    curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs -o /tmp/rustup.sh; \
    sh /tmp/rustup.sh -y --default-toolchain nightly --profile minimal --component rust-src; \
    rm /tmp/rustup.sh
ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup --version && rustc --version

# CIA packaging tools (makerom + bannertool) — pinned (version + sha256, identical to
# .github/workflows/release.yml), x86_64 prebuilts.
# Used only when this image is built for amd64 (see 3ds/build-cia.sh); harmless
# (unused) on arm64. NOT executed at build time (x86_64 can't run on an arm64 build host).
RUN set -eux; \
    curl -fsSL "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.18.4/makerom-v0.18.4-ubuntu_x86_64.zip" -o /tmp/makerom.zip; \
    echo "dd596854718c195c6e3229286be485b122921715555af8ae5cf8e9a465d9f970  /tmp/makerom.zip" | sha256sum -c -; \
    mkdir -p /tmp/mr; unzip -o /tmp/makerom.zip -d /tmp/mr; \
    install -m755 "$(find /tmp/mr -name makerom -type f | head -1)" /usr/local/bin/makerom; \
    rm -rf /tmp/makerom.zip /tmp/mr; \
    curl -fsSL "https://github.com/Epicpkmn11/bannertool/releases/download/v1.2.2/bannertool.zip" -o /tmp/bannertool.zip; \
    echo "e4259c08fe8944ebadd5f4b96f9a8603e5427338074cfc46323fbbc3410d51ed  /tmp/bannertool.zip" | sha256sum -c -; \
    mkdir -p /tmp/bt; unzip -o /tmp/bannertool.zip linux-x86_64/bannertool -d /tmp/bt; \
    install -m755 /tmp/bt/linux-x86_64/bannertool /usr/local/bin/bannertool; \
    rm -rf /tmp/bannertool.zip /tmp/bt
