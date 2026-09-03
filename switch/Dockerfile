# TODO: pin to a date-based tag (e.g. devkitpro/devkita64:20240529) for
# reproducible builds once a stable reference tag is chosen.
FROM devkitpro/devkita64:latest

RUN apt-get update -y \
    && apt-get install -y --no-install-recommends curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Optional: inject local CA certs (gitignored switch/certs/*.pem) so HTTPS works
# behind an SSL-inspecting proxy (e.g. Zscaler). No-op when no certs are present.
# dkp-pacman uses its OWN cert bundle, so the CA must be added there too.
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

# switch-curl: libcurl for Switch homebrew (WebDAV HTTP client)
# deko3d + uam + libdeko3d ship in the devkita64 base image; no extra portlib needed for the borealis deko3d backend.
RUN dkp-pacman -Syu --noconfirm && dkp-pacman -S --noconfirm switch-curl

# Nightly toolchain + rust-src (for -Zbuild-std). Fail loudly on a broken download.
RUN set -eux; \
    curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs -o /tmp/rustup.sh; \
    sh /tmp/rustup.sh -y --default-toolchain nightly --profile minimal --component rust-src; \
    rm /tmp/rustup.sh
ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup --version && rustc --version
