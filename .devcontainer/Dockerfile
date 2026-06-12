FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        gdb \
        pkg-config \
        git \
        ca-certificates \
        curl \
        libglfw3 \
        libglfw3-dev \
        libx11-dev \
        libxcursor-dev \
        libxrandr-dev \
        libxinerama-dev \
        libxi-dev \
        libxext-dev \
        libxfixes-dev \
        libwayland-dev \
        libxkbcommon-dev \
        libgl1-mesa-dev \
        libasound2-dev \
        libdbus-1-dev \
        libpulse0 \
        fonts-dejavu-core \
        mesa-utils \
    && rm -rf /var/lib/apt/lists/*
