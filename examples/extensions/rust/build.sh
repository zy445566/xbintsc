#!/usr/bin/env bash
# Build the Rust extension into a static library that xbintsc links.
#
#   ./build.sh                              # host target
#   RUST_TARGET=x86_64-pc-windows-msvc ./build.sh
#
# Requires a Rust toolchain (`cargo`). The archive is copied to the canonical
# `target/release/libmathx.a`, which `xbintsc.manifest.json` points at.
#
# On Windows, xbintsc links with the MSVC ABI, so build the
# `*-pc-windows-msvc` target to match the system clang.
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
  dir="target/$target/release"
else
  cargo build --release
  dir="target/release"
fi

# Rust names the static library `libmathx.a` on Unix/MinGW but `mathx.lib` on
# Windows MSVC; accept either.
built=""
for candidate in "$dir/libmathx.a" "$dir/mathx.lib"; do
  if [ -f "$candidate" ]; then
    built="$candidate"
    break
  fi
done
if [ -z "$built" ]; then
  echo "expected a static library under $here/$dir after the build" >&2
  exit 1
fi

# Mirror cross-target output to the path the manifest references.
archive="target/release/libmathx.a"
mkdir -p "$(dirname "$archive")"
if [ "$built" != "$archive" ]; then
  cp "$built" "$archive"
fi

echo "built $here/$archive"
