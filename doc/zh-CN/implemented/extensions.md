# 扩展机制（Extensions）

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/extensions.md)

实现位置：`src/extensions/registry.ts`、`src/extensions/node/`、`src/extensions/link-feature.ts`、`src/driver/usage.ts`、`src/driver/link-features.ts`

- 扩展是普通对象：`runtimeSources()`（额外 C 源）、`linkerFlags()`（额外链接参数）、`builtins()`（全局标识符 → 运行时符号，统一 `(argc, argv)` ABI）、`modules()`（导入说明符 → 命名导出 / 命名空间）与 `linkFeatures()`（可选的重型能力）。
- `ExtensionRegistry`：注册 / 注销 / 查询 / 汇总 builtins、modules、runtime 源、链接参数与链接特性。
- **可选链接特性**（`media`、`gpu`、`images`、`shaping`、`webgl`）：
  - 特性声明 `runtimeSources()`（只在启用时编译的 C bridge）、`nativeObjects()`（只在启用时链接的归档）、`linkerFlags()`、可选的 `requires`（依赖的另一个特性）与 `used(usage)`。
  - driver 从打包后的 AST 构造 `ProgramUsage`（`src/driver/usage.ts` 的 `collectUsage`）：字符串字面量（含导入 HTML 与内联 CSS）、标识符/属性名、`getContext(...)` 的 kind，以及 markup 是否可能在运行时拼出。简单的 `const` 别名会被解析；无法解析的 markup 让程序走保守回退。
  - `resolveLinkFeatures` 应用 `--feature` / `--no-feature`（以及 `all`），展开 `requires`，把自动检测到但不可用的特性报为 warning、把强制开启却不可用的报为 error，并返回要链接的 bridge/归档/参数。
  - bridge 就是强引用：构造函数安装特性的分发表，其引用迫使链接器取出实现；没有 bridge 时表保持 null，实现成员永不被链接。
  - 当前客户端：`media`（FFmpeg，`runtime/ext_media/media_bridge.c` + `media.a`）、`gpu`（glslang/SPIRV-Cross，`runtime/ext_gui/gpu_bridge.c` + `gui.a` 内的 `gpu_feature.cpp`）、`images`（stb_image/NanoSVG，`runtime/ext_gui/image_bridge.c` + `image_feature.cpp`）、`shaping`（HarfBuzz/FreeType，`runtime/ext_gui/text_bridge.c` + `text_hb.cpp`）与 `webgl`（context/DOM 绑定，`runtime/ext_gui/webgl_bridge.c` + `webgl_feature.cpp`，依赖 `gpu`）。
- 内置核心扩展 `core`：暴露 `print`（映射 `xt_println`），始终注册。
- Node 扩展 `node`：
  - 模块化组织：`src/extensions/node/fs/` + `runtime/ext_node/fs/read_file.c`
  - 暴露可导入模块（`fs`、`fs/promises`、`path`、`os`、`process` 等），同时支持裸名称与 `node:` 前缀；`import { readFileSync } from "fs"` 解析到 `xt_node_read_text_file`，`path`/`os`/`process` 的导出映射到命名空间分发器。
- 添加新模块只需新增目录 + C 实现，核心编译器无需改动。
- 原生 C++/Rust 扩展（`src/extensions/native.ts`、`--ext-native`）：
  - `nativeObjects()` 链接以 `(argc, argv)` ABI 暴露 `extern "C"` 符号的预编译对象/静态库；JSON manifest 把它们映射为 builtins/modules（`linkerFlags` / `linkerFlagsByPlatform` 用于 C++/Rust 运行时）。
  - 编写辅助位于 `runtime/xt_ext.h`（C/C++）与 `runtime/xt_ext.rs`（Rust）；可运行工程在 `examples/extensions/`。
  - 驱动原样链接这些产物，增量缓存会对其内容取指纹，因此重建库会使缓存二进制失效。
  - CI 在 Linux、macOS、Windows 上都会构建两种语言的示例；Windows 下系统 clang 链接 MSVC ABI（C++ `-lmsvcprt`，Rust `*-pc-windows-msvc` 加 Windows 系统库）。
- Node 模块覆盖情况见 [Node 扩展](./node.md)。
