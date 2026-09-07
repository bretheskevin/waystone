FROM devkitpro/devkitarm:latest

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends curl ca-certificates \
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
