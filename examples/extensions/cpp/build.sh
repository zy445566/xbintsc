#!/usr/bin/env bash
# Build the C++ extension into an object file that xbintsc links.
#
#   ./build.sh                 # uses clang++ (or $CXX)
#   CXX=/path/to/clang++ ./build.sh
#
# The result is `build/mathx.o`, which `xbintsc.manifest.json` points at. On
# Windows the object must use the MSVC ABI, so only an MSVC-targeting clang++ is
# accepted; other Windows C++ toolchains cannot link with the xbintsc runtime.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
runtime="$root/runtime"

# In a Windows POSIX shell a native clang needs Windows-style paths.
# `cygpath -m` keeps forward slashes (`C:/dir`), which those tools accept.
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

is_windows=0
case "$(uname -s)" in
  *NT-* | CYGWIN*) is_windows=1 ;;
esac

if [ -n "${CXX:-}" ]; then
  :
elif [ "$is_windows" = 1 ]; then
  CXX="$(find_tool clang++)" || {
    echo "no clang++ found; install LLVM or set CXX to an MSVC-targeting clang++" >&2
    exit 1
  }
else
  CXX="$(find_tool clang++)" || {
    echo "no clang++ found (install LLVM or set CXX)" >&2
    exit 1
  }
fi

mkdir -p "$here/build"

# Position-independent code is the Unix default; the MSVC ABI (Windows) rejects
# `-fPIC` outright, so only pass it on non-Windows hosts.
pic_flag="-fPIC"
if [ "$is_windows" = 1 ]; then pic_flag=""; fi

# shellcheck disable=SC2086  # $pic_flag must split into an optional flag.
"$CXX" -O2 $pic_flag -Wall -Wextra -I"$runtime" -c "$here/mathx.cpp" -o "$here/build/mathx.o"

echo "built $here/build/mathx.o with $CXX"
