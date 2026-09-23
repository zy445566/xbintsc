#!/usr/bin/env bash
# Build the C++ extension into a static archive that xbintsc links.
#
#   ./build.sh                 # auto-detects clang++/g++ and llvm-ar/ar
#   CXX=g++-13 AR=ar ./build.sh
#
# The result is `build/libmathx.a`, which `xbintsc.manifest.json` points at.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
runtime="$root/runtime"

# On Git Bash/MSYS a native clang needs Windows-style paths. `cygpath -m` keeps
# forward slashes (`C:/dir`), which both MSYS tools and clang accept.
if command -v cygpath >/dev/null 2>&1; then
  here="$(cygpath -m "$here")"
  runtime="$(cygpath -m "$runtime")"
fi

find_tool() {
  for candidate in "$@"; do
    if command -v "$candidate" >/dev/null 2>&1; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

CXX="${CXX:-$(find_tool clang++ g++ c++)}" || {
  echo "no C++ compiler found (set CXX to clang++/g++)" >&2
  exit 1
}
AR="${AR:-$(find_tool llvm-ar ar)}" || {
  echo "no archiver found (set AR to llvm-ar/ar)" >&2
  exit 1
}

mkdir -p "$here/build"
"$CXX" -O2 -fPIC -Wall -Wextra -I"$runtime" -c "$here/mathx.cpp" -o "$here/build/mathx.o"
"$AR" rcs "$here/build/libmathx.a" "$here/build/mathx.o"

echo "built $here/build/libmathx.a with $CXX"
