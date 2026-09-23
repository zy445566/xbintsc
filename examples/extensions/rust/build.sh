#!/usr/bin/env bash
# Build the Rust extension into a static library that xbintsc links.
#
#   ./build.sh                              # host target
#   RUST_TARGET=x86_64-pc-windows-gnullvm ./build.sh
#
# Requires a Rust toolchain (`cargo`). The archive is copied to the canonical
# `target/release/libmathx.a`, which `xbintsc.manifest.json` points at.
#
# On Windows the bundled MinGW-w64 toolchain is the linker, so build the
# `*-pc-windows-gnullvm` target (the `*-pc-windows-msvc` target produces MSVC
# objects the MinGW linker cannot consume).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$here"

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo not found; install Rust from https://rustup.rs" >&2
  exit 1
fi

target="${RUST_TARGET:-}"
if [ -n "$target" ]; then
  cargo build --release --target "$target"
  built="target/$target/release/libmathx.a"
else
  cargo build --release
  built="target/release/libmathx.a"
fi

if [ ! -f "$built" ]; then
  echo "expected $here/$built to exist after the build" >&2
  exit 1
fi

# Mirror cross-target output to the path the manifest references.
archive="target/release/libmathx.a"
mkdir -p "$(dirname "$archive")"
if [ "$built" != "$archive" ]; then
  cp "$built" "$archive"
fi

echo "built $here/$archive"
