# xbintsc Requirements

This page lists what a machine needs in order for `xbintsc` to **compile and
link native binaries**. xbintsc does not bundle a compiler, so every host needs
a clang-compatible toolchain installed. `xbintsc emit` (pure LLVM IR text) has
no requirements on any platform.

## Minimum versions

| Requirement | Minimum | Notes |
| --- | --- | --- |
| clang / LLVM | **16** | The emitted IR uses opaque pointers, which LLVM 16 made the only pointer representation. |
| Node.js | **22** | Required to run the compiler from npm or from source; released binaries are standalone and need no Node. |

Installing the compiler from npm instead of using a release archive? Node.js 22+
then runs the compiler itself; see [Using xbintsc from npm](./npm-package.md).

clang 15 and older fail on the first string literal in any program, with:

```
build/app.ll:382:37: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'
  %r0 = call i64 @xt_string_new(i8* @.str.0, i64 5)
```

Typed pointers were removed in LLVM 16 ([release notes](https://github.com/llvm/llvm-project/blob/release/15.x/llvm/docs/ReleaseNotes.rst#changes-to-the-llvm-ir)),
so xbintsc now **checks the version before it compiles anything**:

- `xbintsc build` / `run` stop immediately with an actionable error when the
  resolved driver is older than 16, instead of emitting IR that cannot compile.
- `xbintsc doctor` prints `status : ok` or `status : too old` and exits non-zero
  when the toolchain cannot build binaries, so scripts and CI can preflight it.
- The search prefers a clang >= 16: on Windows the LLVM bundled with Visual
  Studio (`<VS install>\VC\Tools\Llvm\x64\bin\clang.exe`) is found even when an
  older `clang` comes first on `PATH`. Set `xbintsc_CLANG` to override the
  choice; an explicit older binary is rejected with a "too old" error rather
  than silently ignored.

Working around the limitation by compiling the IR elsewhere is not supported.

## Summary

| Platform | Requirement | Provided by |
| --- | --- | --- |
| Linux | `clang` + `lld` (a glibc distribution) | the user (package manager) |
| macOS | **Xcode Command Line Tools** | the user (install once) |
| Windows | **LLVM** + **Visual Studio C++ build tools** (MSVC ABI) | the user (install once) |
| any | `xbintsc emit` | nothing |

The Windows release targets the **MSVC ABI**: clang uses the
Windows SDK and the MSVC C/C++ runtime/headers.

Optional optimization features add two tools:

- `--lto` needs **lld** (`-fuse-ld=lld`); the LLVM installs above include it.
- `--pgo-use` needs **llvm-profdata** to merge `.profraw` files. The driver
  prefers the copy next to the resolved clang; override it with
  `xbintsc_LLVM_PROFDATA`.

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

Every archive carries the prebuilt runtime for its host under
`runtime/lib/<os>-<arch>/`: `core` and `ext_node` on all platforms (`.lib`
instead of `.a` with the MSVC ABI on Windows), plus `gui`/`media` where the
release builds them. The npm package receives the same archives from its
optional `@xbintsc/<os>-<arch>` dependency, so an npm install does not compile
the C runtime either.

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

## Windows — install the MSVC C++ build tools (with LLVM)

xbintsc targets the **MSVC ABI** on Windows. One `winget` command installs the
Visual Studio C++ workload, MSBuild and the Clang component that the local build
scripts and CI use:

```powershell
winget install -e --id Microsoft.VisualStudio.2022.BuildTools `
  --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --includeRecommended"
```

The LLVM bundled with Visual Studio 2022
(`VC\Tools\Llvm\x64\bin\clang.exe`, version 19.1.5 in current installs) is what
`scripts/setup-vs-toolchain.ps1` selects. A standalone LLVM also works for
`xbintsc` itself (`winget install -e --id LLVM.LLVM`), but the GUI/media archive
builds and the CI setup use the Visual Studio one so one toolchain produces the
runtime, the archives and the final binary. Either way the compiler must target
the MSVC ABI (`Target: *-windows-msvc`): clang built for another Windows ABI
cannot link with the MSVC runtime, so `xbintsc` rejects it with an explicit
error.

A developer prompt is normally **not** required: `xbintsc` resolves the Visual
Studio LLVM by absolute path, and that clang locates the MSVC/SDK headers,
libraries and linker itself. If your installation still needs the environment
imported, run the build from an **x64 Native Tools Command Prompt for VS 2022**
(or **ARM64 Native Tools** on Windows on ARM), or import it manually:

```powershell
& "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
```

In a source checkout, `pwsh -File scripts/setup-vs-toolchain.ps1` does the same
and also pins `xbintsc_CLANG` to the LLVM bundled with that Visual Studio
installation. CI calls the same script, so a local build, the release archives
and CI use one Visual Studio toolchain.

Verify with `xbintsc doctor` (or `clang --version`); the report names the
resolved compiler, its version and its target.

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
