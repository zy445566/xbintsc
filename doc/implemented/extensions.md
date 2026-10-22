# Extension mechanism

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/extensions.md)

Location: `src/extensions/registry.ts`, `src/extensions/node/`

- An extension is a plain object: `runtimeSources()` (extra C sources), `linkerFlags()` (extra link flags), `builtins()` (global identifier → runtime symbol, uniform `(argc, argv)` ABI) and `modules()` (import specifier → named exports / namespace).
- `ExtensionRegistry`: register / unregister / lookup / aggregate builtins, modules, runtime sources and link flags.
- The built-in core extension `core`: exposes `print` (mapped to `xt_println`), always registered.
- The Node extension `node`:
  - Modular organisation: `src/extensions/node/fs/` + `runtime/ext_node/fs/read_file.c`
  - Exposes importable modules (`fs`, `fs/promises`, `path`, `os`, `process`, ...) under both bare and `node:`-prefixed specifiers; `import { readFileSync } from "fs"` resolves to `xt_node_read_text_file`, and `path`/`os`/`process` map exports onto the namespace dispatchers.
- Adding a new module only requires a new directory plus a C implementation; the core compiler never changes.
- Native C++/Rust extensions (`src/extensions/native.ts`, `--ext-native`):
  - `nativeObjects()` links pre-built objects/static archives that expose `extern "C"` symbols with the `(argc, argv)` ABI; a JSON manifest maps them onto builtins/modules (`linkerFlags` / `linkerFlagsByPlatform` cover C++/Rust runtimes).
  - Authoring helpers live in `runtime/xt_ext.h` (C/C++) and `runtime/xt_ext.rs` (Rust); runnable projects are under `examples/extensions/`.
  - The driver links the artifacts verbatim and the incremental cache fingerprints their contents, so a rebuilt library invalidates the cached binary.
  - CI builds both language examples on Linux, macOS and Windows; on Windows the system clang links with the MSVC ABI (C++ `-lmsvcprt`, Rust `*-pc-windows-msvc` plus the Windows system libraries).
- Node module coverage is documented in [Node extension](./node.md).
