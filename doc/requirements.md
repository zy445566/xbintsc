# xbintsc Requirements

This page lists what a machine needs in order for `xbintsc` to **compile and
link native binaries**. xbintsc does not bundle a compiler, so every host needs
a clang-compatible toolchain installed. `xbintsc emit` (pure LLVM IR text) has
no requirements on any platform.

## Minimum versions

| Requirement | Minimum | Notes |
| --- | --- | --- |
| clang / LLVM | **16** | The emitted IR uses opaque pointers, which LLVM 16 made the only pointer representation. |
| Node.js | **22** | Only to run or build the compiler from source; released binaries are standalone and need no Node. |

clang 15 and older fail on the first string literal in any program, with:

```
build/app.ll:382:37: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'
  %r0 = call i64 @xt_string_new(i8* @.str.0, i64 5)
```

Typed pointers were removed in LLVM 16 ([release notes](https://github.com/llvm/llvm-project/blob/release/15.x/llvm/docs/ReleaseNotes.rst#changes-to-the-llvm-ir)),
so check `clang --version` before anything else, and point xbintsc at a newer
build with `xbintsc_CLANG` when the system clang is older. `xbintsc doctor`
reports the resolved toolchain and its version. Working around the limitation by
compiling the IR elsewhere is not supported.

## Summary

| Platform | Requirement | Provided by |
| --- | --- | --- |
| Linux | `clang` + `lld` (a glibc distribution) | the user (package manager) |
| macOS | **Xcode Command Line Tools** | the user (install once) |
| Windows | **LLVM** + **Visual Studio C++ build tools** (MSVC ABI) | the user (install once) |
| any | `xbintsc emit` | nothing |

The Windows release targets the **MSVC ABI**: clang uses the
Windows SDK and the MSVC C/C++ runtime/headers.

## Prebuilt releases (recommended)

Download the archive for your platform from
[GitHub Releases](https://github.com/zy445566/xbintsc/releases/latest) (each one
has a `.sha256` beside it):

| Platform | Archive |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-<version>-win32-x64.tar.zst` / `xbintsc-<version>-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-<version>-linux-x64.tar.zst` / `xbintsc-<version>-linux-arm64.tar.zst` |
| macOS x64 / arm64 (Apple Silicon) | `xbintsc-<version>-darwin-x64.tar.zst` / `xbintsc-<version>-darwin-arm64.tar.zst` |

(`.tar.gz` is used only when `zstd` is unavailable on the build machine.)

`<version>` is the release version (for example `0.3.14`), embedded in the asset
name so downloads from different releases do not collide.

Every archive unpacks to the same layout:

```text
xbintsc-<os>-<arch>/
  bin/xbintsc[.exe]      the compiler
  runtime/               C runtime sources + prebuilt runtime/lib/<os>-<arch>/
```

Windows archives omit the prebuilt `runtime/lib/`; the driver compiles the C
runtime on demand with the user's clang.

Unpack it, put `bin/` on `PATH` (or call `bin/xbintsc` directly), and install the
toolchain for your platform below. Verify the download against the checksum
first:

```sh
sha256sum -c xbintsc-<version>-linux-x64.tar.zst.sha256   # macOS: shasum -a 256 -c
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

## Linux — install clang and lld

A normal glibc-based distribution plus `clang` and `lld` is enough:

```sh
sudo apt-get install -y clang lld        # Debian / Ubuntu
sudo dnf install -y clang lld            # Fedora / RHEL
sudo pacman -S --needed clang lld        # Arch
sudo zypper install -y clang lld         # openSUSE
sudo apk add clang lld                   # Alpine
```

Verify with `clang --version`. If xbintsc uses the wrong linker, set
`xbintsc_CLANG` to the clang you want and/or pass extra flags through
`xbintsc_LINKER_ARGS` (for example `-fuse-ld=lld`).

## Windows — install LLVM and the MSVC C++ build tools

Install LLVM and the Visual Studio C++ build tools with `winget`:

```powershell
winget install -e --id LLVM.LLVM
winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

clang needs the MSVC/SDK environment (`INCLUDE`, `LIB`, `PATH`) to find the C
standard library headers, the import libraries and the linker. Open an
**x64 Native Tools Command Prompt for VS 2022** (or an **ARM64 Native Tools**
prompt on Windows on ARM) before running `xbintsc`, or import the environment
yourself:

```powershell
& "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
```

Verify with `clang --version` (chocolatey also works: `choco install llvm`).

## Building from source (contributors only)

- Node.js ≥ 22 is required only to run/build the compiler itself. The released
  standalone binaries do **not** need Node.
- A C compiler is required to rebuild the runtime (`npm run runtime`).
- `npm run package-release` assembles the per-platform release archive
  (`xbintsc-<os>-<arch>.tar.{gz,zst}` + `.sha256`) into `dist/release/`.

## Checking your environment

```sh
xbintsc doctor
```

`doctor` reports the resolved toolchain (env / system), its version and location,
and the runtime-library directory.
