# xbintsc Self-Contained Roadmap (no user-installed tools)

> Language: **English** | [简体中文](./zh-CN/self-contained-roadmap.md)

The goal is that an end user can `build` / `run` with **only xbintsc installed**,
without installing `clang` / `gcc` / `ld` / `llc` themselves.

> **Decisions (recorded):**
> - **macOS**: users are expected to install the **Xcode Command Line Tools**.
>   xbintsc therefore uses the system clang / linker / SDK on macOS and does
>   **not** bundle a toolchain there. See [`requirements.md`](./requirements.md).
> - **Windows**: xbintsc ships a **MinGW-w64 ABI** toolchain (clang + lld + CRT +
>   import libraries), so no user-installed compiler is required.

## 1. Goals and non-goals

**Goals**

- On a clean machine (only the OS-provided libc), unzip/install `xbintsc` and
  then `build` + `run` works.
- Consistent on Linux / macOS / Windows.
- Keep `emit` (LLVM IR text only) dependency-free.

**Non-goals (this stage)**

- Cross-compilation: host platform only for now.
- Eliminating the OS libc: dynamic libc is part of the OS, not a user-installed
  tool. (Optional `-static` / musl is P5.)

## 2. Current state

| Stage | Implementation | External dependency |
| --- | --- | --- |
| Front end TS→IR | xbintsc itself (already self-hosted) | none |
| IR→object | `clang -c` (`src/driver/toolchain.ts` `compileIr`) | clang |
| C runtime→object | `clang -c` compiles 22+ `.c` at runtime (`src/driver/compiler.ts` `ensureRuntimeObjects`) | clang + C headers |
| Link | `clang ... -o` (which then calls system `ld`/`ld64`/`link.exe`) | clang + system linker + libc/CRT |

In short: `build` / `run` hard-depend on clang; `emit` is already dependency-free.
See "Replacing llc" in `doc/DESIGN.md`: clang replaced `llc` precisely to avoid
requiring it.

## 3. Options and choice

| Option | Approach | Size | Effort | Robustness | Verdict |
| --- | --- | --- | --- | --- | --- |
| A System toolchain | status quo, user installs clang | 0 | 0 | low | misses the goal |
| **B Bundled toolchain (Zig-style)** | ship `clang`/`lld` + prebuilt runtime | large | medium | high | ⭐ **recommended spine** |
| C libLLVM/liblld in-process (Rust-style) | LLVM-C emits `.o`, liblld links | medium | large | medium | long-term optimization |
| D Own backend + linker | emit machine code/objects ourselves | small | huge | low | unrealistic |

**Choice: B as the spine, C reserved.** B is the smallest change that reaches the
goal and reuses the existing self-host artifacts. C shrinks the bundle further but
is expensive to integrate with a TS/Node stack; it is an optional P4 evolution.
Note: `lld` is **only a linker** — it cannot replace clang's "IR→obj" and "C→obj"
roles, so every option must assign those two somewhere.

## 4. Target architecture

```
xbintsc (self-contained)
├── compiler core   TS → LLVM IR text            # exists, self-hosted
├── runtime libs    runtime/lib/<os>-<arch>/*.a(.lib)   # new: prebuilt in CI
├── toolchain abstraction  src/driver/toolchain-provider.ts   # new: pluggable
│     ├── IR lowerer   IR → .o   (bundled llc/clang; later libLLVM)
│     ├── C compiler   .c → .o   (fallback only; default uses prebuilt libs)
│     └── linker       .o → exe  (bundled lld; later liblld)
└── vendor/<os>-<arch>/   clang/lld/CRT/import libs      # new: shipped
```

Toolchain resolution order (new `resolveToolchain()`):

1. env overrides: `xbintsc_TOOLCHAIN` / `xbintsc_CLANG` / `xbintsc_LLD`
2. shipped `vendor/<os>-<arch>/`
3. system `PATH` (dev-friendly, can be disabled)

Fail with a clear message and point at `xbintsc doctor` when nothing resolves.

## 5. Phased plan

### P0 Toolchain abstraction + prebuilt runtime (foundation, biggest win)

- New `src/driver/toolchain-provider.ts`: `Lowerer` / `CCompiler` / `Linker` /
  `Toolchain` interfaces plus `resolveToolchain()`; fold the low-level commands
  in `toolchain.ts` into concrete implementations.
- Rework `scripts/build-runtime.mjs` to emit a **static archive**
  `runtime/lib/<os>-<arch>/libxbintsc_runtime.a` (`.lib` on Windows); extensions
  may become separate archives linked on demand.
- Change `src/driver/compiler.ts` `ensureRuntimeObjects()` to **prefer the
  prebuilt archive**, falling back to compiling `.c` only if missing.
- Change `src/driver/paths.ts`: add `findRuntimeLibDir()` / `findVendorDir()`.
- Acceptance: `npm run runtime` produces the archive; `build` still links after
  deleting `build/runtime/*.o`; `emit` works with no clang; first `build` no longer
  compiles 22 C files (faster).

> A prebuilt runtime is the precondition for every option: it removes the need for
> C headers / SDKs at runtime and dramatically shortens the first build.

### P1 Bundled toolchain

- CI fetches `clang`/`lld` from the official LLVM release (or `llvm-tools`), trims
  to `clang` (or `llc`) + `lld`, and places them in `vendor/<os>-<arch>/`.
- `resolveToolchain()` invokes the shipped tools by **absolute path**, never `PATH`.
- Platform handling:
  - Linux: bundle clang + lld; use the system glibc.
  - macOS: rely on the system clang / linker / SDK from the **Xcode Command Line
    Tools** (documented prerequisite) — no bundle.
  - Windows: bundle a **MinGW-w64** ABI toolchain (clang + lld + CRT + import
    libs), like Rust's `*-windows-gnu`.
- Acceptance: pure Linux container (no clang/ld), macOS **with** Command Line
  Tools, Windows without Visual Studio — `build` + `run` all pass.

### P2 Distribution

- `release.yml`: publish `xbintsc-<os>-<arch>.tar.zst` containing
  `bin/xbintsc` + `vendor/` + `runtime/lib/`.
- npm: per-platform `optionalDependencies` (`@xbintsc/<platform>`) or a postinstall
  downloader (sha256-verified). The launcher (`bin/xbintsc.js`) locates `vendor/`.
- New `xbintsc doctor`: prints toolchain source, version, path, runtime-lib location.

### P3 Platform hardening

- **macOS**: ad-hoc codesign the vendored binaries (`codesign -s -`); clear the
  quarantine attribute on first run; `ld64.lld` needs the SDK's `libSystem.tbd`
  (see §6).
- **Windows**: pick the ABI (MinGW-w64 preferred, to bundle CRT/import libs and
  avoid the MSVC SDK); replace `-lws2_32` etc. with bundled import libs.
- **Linux**: optional `-static` (musl).

### P4 (optional, long-term) libLLVM / liblld in-process

- Write a **Node-API native addon** wrapping **LLVM-C**
  (`LLVMTargetMachineEmitToFile`) to replace `llc`/`clang` for object emission and
  shrink the bundle.
- Further wrap `liblld` (no stable C API; needs C++ glue) for in-process linking,
  approaching a single-binary "rustc-style" tool. Acceptance: `vendor/` holds only
  libLLVM/liblld.

### P5 (optional) Fully static / no libc

- Linux: bundle musl CRT/libc for `-static`.
- Further `-nostdlib` + direct-syscall runtime (very large; a separate project).

## 6. Platform risks

| Platform | Risk | Mitigation |
| --- | --- | --- |
| macOS | Downloaded bundle is quarantined / blocked by Gatekeeper; Apple Silicon needs signing | Rely on the Command Line Tools (no bundle on macOS), so no quarantine on our binaries |
| macOS | `ld64.lld` needs the SDK's `libSystem.tbd`, which comes from Command Line Tools | Rely on Command Line Tools as a documented prerequisite ([`requirements.md`](./requirements.md)); no `.tbd` redistribution |
| Windows | MSVC path depends on Windows SDK import libs/CRT the user may lack | Ship the MinGW-w64 ABI with bundled CRT and import libs (no MSVC SDK) |
| General | Bundle size / npm package limits | Per-platform packages / release archives; `tar.zst` |
| General | Emitted IR vs vendored LLVM version skew | Pin the LLVM version; fold it into the version constant and cache key |
| General | Cross-compile matrix explosion | Host-only first; state it explicitly |

## 7. Overall acceptance criteria

1. On a clean machine (only OS libc; on macOS the Command Line Tools per
   `requirements.md`): unzip the release and `xbintsc build` / `run` works.
2. A new CI **"no system toolchain" job** hides clang/ld/llc from `PATH` (or uses a
   minimal container) and verifies `build` + `run` still succeed.
3. `emit` remains fully dependency-free.
4. `xbintsc doctor` clearly reports where the toolchain came from.

## 8. Milestones (relative order, no dates)

| Milestone | Content | Deliverable |
| --- | --- | --- |
| M1 | P0 done | Prebuilt runtime archive + toolchain abstraction |
| M2 | P1 done | Bundled toolchain on all 3 OSes; clean-environment builds pass |
| M3 | P2 done | Release archives + npm distribution + `doctor` |
| M4 | P3 done | macOS/Windows hardened; fully self-contained |
| M5 | P4/P5 (optional) | libLLVM-ized / fully static |

## 9. Key decisions

- **Prebuilding the runtime into a library** is the precondition for every option.
- Toolchain resolution must be **pluggable and overridable**: bundled by default,
  system fallback allowed.
- Evolution order: **make it run (bundle clang+lld) → make it small (libLLVM+lld)
  → make it pure (own backend)**.
- `lld` covers only the *link* step; the plan must explicitly place IR→obj and
  C→obj.
