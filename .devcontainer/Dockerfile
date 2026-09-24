# Node 24 matches the release workflow. Debian trixie ships libcrypt.so.1, so
# electron-builder's bundled fpm can build the .deb here (unlike on Fedora).
FROM mcr.microsoft.com/devcontainers/javascript-node:5-24-trixie

# Shared libraries Electron needs to launch. The desktop-lite feature supplies
# the X server and window manager; these cover the rest of Chromium's runtime.
RUN apt-get update \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        libasound2t64 \
        libatk-bridge2.0-0t64 \
        libcups2t64 \
        libgbm1 \
        libgtk-3-0t64 \
        libnotify4 \
        libnss3 \
        libsecret-1-0 \
        libxss1 \
        libxtst6 \
        xdg-utils \
    && rm -rf /var/lib/apt/lists/*

# Mount point for the download cache volume (Electron, electron-builder tools).
# Creating it here lets the volume inherit the node user's ownership.
RUN mkdir -p /home/node/.cache && chown node:node /home/node/.cache
