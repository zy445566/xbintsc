# xbintsc Requirements

This page lists what a machine needs in order for `xbintsc` to **compile and
link native binaries**. Only the toolchain is platform-sensitive; `xbintsc emit`
(pure LLVM IR text) has no requirements on any platform.

## Summary

| Platform | Requirement | Provided by |
| --- | --- | --- |
| Linux | system C library (glibc) | the OS |
| macOS | **Xcode Command Line Tools** | the user (install once) |
| Windows | none beyond the OS | xbintsc ships a MinGW-w64 ABI toolchain |
| any | `xbintsc emit` | nothing |

## Prebuilt releases (recommended)

Download the self-contained archive for your platform from
[GitHub Releases](https://github.com/zy445566/xbintsc/releases/latest) (each one
has a `.sha256` beside it):

| Platform | Archive |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-win32-x64.tar.zst` / `xbintsc-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-linux-x64.tar.zst` / `xbintsc-linux-arm64.tar.zst` |
| macOS x64 / arm64 (Apple Silicon) | `xbintsc-darwin-x64.tar.zst` / `xbintsc-darwin-arm64.tar.zst` |

(`.tar.gz` is used only when `zstd` is unavailable on the build machine.)

Every archive unpacks to the same layout:

```text
xbintsc-<os>-<arch>/
  bin/xbintsc[.exe]      the compiler
  runtime/               C runtime + prebuilt runtime/lib/<os>-<arch>/
  vendor/<os>-<arch>/    bundled toolchain (Linux/Windows)
```

Unpack it and put `bin/` on `PATH` (or call `bin/xbintsc` directly) — there is no
install step. Verify the download against the checksum first:

```sh
sha256sum -c xbintsc-linux-x64.tar.zst.sha256   # macOS: shasum -a 256 -c
```

## macOS — install Xcode Command Line Tools

xbintsc uses the linker and system SDK (`libSystem`, …) that ship with the
Command Line Tools to produce executables. Install them once:

```sh
xcode-select --install
```

Verify:

```sh
xcode-select -p            # prints the active developer directory
xcrun --show-sdk-path      # prints the SDK path
```

If they are missing, `xbintsc build` fails with a clear message pointing here.
`xbintsc emit` still works without them.

## Linux

A normal glibc-based distribution is enough — nothing to install. The bundle
ships clang + lld together with the shared libraries they need (notably the
legacy `libtinfo.so.5`), so a system without a compiler works out of the box.
Static binaries (optional, later) will use a bundled musl CRT.

## Windows

Nothing to install. xbintsc ships a **MinGW-w64** ABI toolchain (clang + lld +
CRT + import libraries) with the release, so `xbintsc build` works on a clean
Windows install.

## Building from source (contributors only)

- Node.js ≥ 22 is required only to run/build the compiler itself. The released
  standalone binaries do **not** need Node.
- A C compiler is required only to rebuild the runtime
  (`npm run runtime`) — not to **use** a released xbintsc.
- `npm run fetch-toolchain` downloads the toolchain xbintsc bundles and unpacks it
  into `vendor/<os>-<arch>/`; `resolveToolchain()` then prefers it over `PATH`
  (Linux: LLVM, Windows: llvm-mingw). On macOS this is a no-op — the Command Line
  Tools are used. `xbintsc_LINKER_ARGS` / `xbintsc_CLANG` override the result.
- `npm run package-release` assembles the per-platform release archive
  (`xbintsc-<os>-<arch>.tar.{gz,zst}` + `.sha256`) into `dist/release/`.

## Checking your environment

```sh
xbintsc doctor
```

`doctor` reports the resolved toolchain (env / bundled / system), its version and
location, and the runtime-library directory.
