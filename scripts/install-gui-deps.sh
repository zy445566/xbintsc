#!/bin/sh
#
# Install everything Linux needs to build and exercise the GUI archive.
#
# `npm run gui` compiles SDL3 from source, so SDL3 needs to *find* the window
# system it will use: without the X11/Wayland development headers it either
# configures itself without a window backend and then aborts ("SDL could not
# find X11 or Wayland development libraries"), or -- now -- `scripts/build-gui.ts`
# says which package is missing. `cmake` configures the vendored libraries,
# `clang` compiles the runtime and the emitted LLVM IR, `libvulkan-dev` plus a
# driver gives SDL_GPU the Vulkan backend it selects on Linux, and `xvfb` lets
# the example and the e2e suites run headlessly.
#
# One list, one place: every Linux job that runs `npm run gui` (or a GUI
# example) sources this, so a job cannot quietly miss a package the others
# install. Requires sudo and a Debian/Ubuntu host.
#
# Usage:  sudo sh scripts/install-gui-deps.sh
set -eu

apt-get update
apt-get install -y \
  clang cmake \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxinerama-dev libxfixes-dev libxkbcommon-dev libwayland-dev \
  wayland-protocols libdecor-0-dev libasound2-dev libpulse-dev \
  libdbus-1-dev libudev-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
  libegl1-mesa-dev libvulkan-dev mesa-vulkan-drivers xvfb
