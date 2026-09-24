#!/usr/bin/env bash
set -euo pipefail

# Docker creates volume mount points as root when they don't exist in the
# image, which is always the case for node_modules inside the workspace.
sudo chown node:node node_modules "$HOME/.cache"

npm ci
