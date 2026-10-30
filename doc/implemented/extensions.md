# Extension mechanism

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/extensions.md)

Location: `src/extensions/registry.ts`, `src/extensions/node/`, `src/extensions/link-feature.ts`, `src/driver/usage.ts`, `src/driver/link-features.ts`

- An extension is a plain object: `runtimeSources()` (extra C sources), `linkerFlags()` (extra link flags), `builtins()` (global identifier → runtime symbol, uniform `(argc, argv)` ABI), `modules()` (import specifier → named exports / namespace) and `linkFeatures()` (optional heavy capabilities).
- `ExtensionRegistry`: register / unregister / lookup / aggregate builtins, modules, runtime sources, link flags and link features.
- **Optional link features** (`media`, `gpu`, `images`, `webgl`):
  - A feature declares `runtimeSources()` (a C bridge compiled only when active), `nativeObjects()` (archives linked only when active), `linkerFlags()`, optional `requires` (another feature it depends on) and `used(usage)`.
  - The driver builds `ProgramUsage` from the bundled AST (`collectUsage` in `src/driver/usage.ts`): string literals (including imported HTML and inlined CSS), identifiers/property names, `getContext(...)` kinds, and whether markup can be built at run time. Simple `const` aliases are resolved; unresolved markup makes the program conservative.
  - `resolveLinkFeatures` applies `--feature` / `--no-feature` (and `all`), expands `requires`, reports an unavailable auto-detected feature as a warning and a forced one as an error, and returns the bridges/archives/flags to link.
  - The bridge is the strong reference: its constructor installs the feature's dispatch table and its references force the linker to extract the implementation. With no bridge, the table stays null and the implementation members are never linked.
  - Current clients: `media` (FFmpeg, `runtime/ext_media/media_bridge.c` + `media.a`), `gpu` (glslang/SPIRV-Cross, `runtime/ext_gui/gpu_bridge.c` + `gpu_feature.cpp` inside `gui.a`), `images` (stb_image/NanoSVG, `runtime/ext_gui/image_bridge.c` + `image_feature.cpp`), and `webgl` (context/DOM bindings, `runtime/ext_gui/webgl_bridge.c` + `webgl_feature.cpp`, requiring `gpu`).
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
