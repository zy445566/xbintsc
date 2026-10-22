# Driver, incremental compilation and toolchain

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/driver.md)

Location: `src/driver/compiler.ts` (orchestration), `src/driver/frontend.ts`,
`src/driver/build-key.ts`, `src/driver/runtime-objects.ts`,
`src/driver/app-artifacts.ts`, `src/driver/cache.ts`, `src/driver/toolchain.ts`,
`src/driver/paths.ts`, `src/driver/version.ts`.

- Compilation pipeline: read source → module bundling (`src/driver/modules.ts`, when the entry contains `import`/`export`) → parse → bind/check → IR → object file → link.
- Incremental cache: keyed on "compiler version + source hash + emit kind + optimization level + LTO/PGO mode + platform + extension fingerprint (names, linker flags, native object contents)"; if the artifacts exist and are fresh, the build is skipped. `--pgo-use` always rebuilds because the profile data can change without the source. The key is assembled in `src/driver/build-key.ts`.
- C runtime and extension sources are cached as object files by content hash and compiled only once (the cache key includes the LTO/PGO flags). `RUNTIME_SOURCES` in `src/driver/runtime-objects.ts` lists the core translation units.
- Toolchain wrapper: locates `clang` (overridable with `xbintsc_CLANG`), compiles IR, compiles C, links.
- LTO (`--lto` / `"lto": true`): the program and runtime compile to LLVM bitcode (`-flto`) and link with `-fuse-ld=lld`; lld is required.
- PGO (`--pgo-generate[=dir]` then `--pgo-use[=dir]`, or `"pgo"` / `"pgoDir"`): the generated IR and runtime C are instrumented with `-fprofile-generate=<dir>`; after running the instrumented binary, `--pgo-use` merges `.profraw` files with `llvm-profdata` (preferring the copy next to clang, overridable with `xbintsc_LLVM_PROFDATA`) and rebuilds with `-fprofile-use=<dir>`.
- Link flags: `-lm` is added automatically on non-Windows; extensions may append extra link flags.
- Project config (`src/driver/config.ts`, `xbintsc.config.json`): `entry`, `outDir`, `output`, `optimize`, `lto`, `pgo`, `pgoDir`, `extensions`, `extNative`, `force` and `app` (`name`, `icon`, `bundle`, `bundleId`). Discovered by walking up from the entry/cwd, overridable with `--config`, disabled with `--no-config`; every path resolves against the config directory; CLI flags win over config values.
- Application icon (`src/driver/icon.ts`, `win-icon.ts`, `mac-bundle.ts`, `--icon`/`app.icon`): PNG/ICO/ICNS is embedded as `xt_app_icon_*` symbols, synthesized into a PE resource via `llvm-rc`/`windres` on Windows, and packaged into `Foo.app` (with `AppIcon.icns` via `sips`/`iconutil`) on macOS. The `gui` extension calls `SDL_SetWindowIcon` from the embedded bytes. `xbintsc_RC` overrides the resource compiler.
- Compiler version: `COMPILER_VERSION` lives in `src/driver/version.ts` and is part of the cache key. The pre-commit hook bumps it (together with `package.json`) only on `main`/`master`, so feature branches never touch the version files.
