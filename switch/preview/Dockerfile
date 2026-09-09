FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    meson \
    ninja-build \
    pkg-config \
    libglfw3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libglm-dev \
    xvfb \
    x11-utils \
    imagemagick \
    mesa-utils \
    libgl1-mesa-dri \
    && rm -rf /var/lib/apt/lists/*

# Force software OpenGL via Mesa llvmpipe (OpenGL 4.5 support — satisfies borealis's GL 4.3 request)
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV GALLIUM_DRIVER=llvmpipe

WORKDIR /work
