#!/usr/bin/env bash
# Build the C++ extension into an object file that xbintsc links.
#
#   ./build.sh                 # auto-detects clang++/g++
#   CXX=g++-13 ./build.sh
#
# The result is `build/mathx.o`, which `xbintsc.manifest.json` points at.
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

mkdir -p "$here/build"
"$CXX" -O2 -fPIC -Wall -Wextra -I"$runtime" -c "$here/mathx.cpp" -o "$here/build/mathx.o"

echo "built $here/build/mathx.o with $CXX"
