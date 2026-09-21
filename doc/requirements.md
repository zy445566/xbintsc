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

A normal glibc-based distribution is enough — nothing to install. Static
binaries (optional, later) will use a bundled musl CRT.

## Windows

Nothing to install. xbintsc ships a **MinGW-w64** ABI toolchain (clang + lld +
CRT + import libraries) with the release, so `xbintsc build` works on a clean
Windows install.

## Building from source (contributors only)

- Node.js ≥ 22 is required only to run/build the compiler itself. The released
  standalone binaries do **not** need Node.
- A C compiler is required only to rebuild the runtime
  (`npm run runtime`) — not to **use** a released xbintsc.

## Checking your environment

```sh
xbintsc doctor
```

`doctor` reports the resolved toolchain (env / bundled / system), its version and
location, and the runtime-library directory.
