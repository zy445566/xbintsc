# 驱动、增量编译与工具链

> 属于[已实现语法与功能](../implemented.md) · [English](../../implemented/driver.md)

实现位置：`src/driver/compiler.ts`（编排）、`src/driver/frontend.ts`、
`src/driver/build-key.ts`、`src/driver/runtime-objects.ts`、
`src/driver/app-artifacts.ts`、`src/driver/cache.ts`、`src/driver/toolchain.ts`、
`src/driver/paths.ts`、`src/driver/version.ts`。

- 编译流水线：读源 → 模块打包（`src/driver/modules.ts`，当入口含 `import`/`export` 时）→ 解析 → 绑定 → [语义检查](./checker.md) → IR → 目标文件 → 链接。
- 增量缓存：以「编译器版本 + 源码哈希 + emit 类型 + 优化级别 + LTO/PGO 模式 + 平台 + `check`/`no-check` + 扩展指纹（名称、链接参数、原生对象内容）」为键，产物存在且新鲜则跳过构建。`--pgo-use` 总是重新构建，因为 profile 数据可能变化而源码不变。缓存键在 `src/driver/build-key.ts` 中组装。
- C 运行时与扩展源按内容哈希缓存目标文件，只编译一次（缓存键包含 LTO/PGO 参数）。核心翻译单元列表 `RUNTIME_SOURCES` 位于 `src/driver/runtime-objects.ts`。
- 工具链封装：查找 `clang`（可用 `xbintsc_CLANG` 覆盖）、编译 IR、编译 C、链接。
- LTO（`--lto` / `"lto": true`）：程序与运行时都编译为 LLVM bitcode（`-flto`），并用 `-fuse-ld=lld` 链接；需要 lld。
- PGO（先 `--pgo-generate[=dir]`，再 `--pgo-use[=dir]`，或配置 `"pgo"` / `"pgoDir"`）：生成 IR 与运行时 C 都用 `-fprofile-generate=<dir>` 插桩；运行插桩后的二进制，`--pgo-use` 会用 `llvm-profdata` 合并 `.profraw`（优先使用 clang 旁边的副本，可用 `xbintsc_LLVM_PROFDATA` 覆盖），再以 `-fprofile-use=<dir>` 重新构建。
- 链接参数：非 Windows 自动加 `-lm`；扩展可追加链接参数。
- 项目编译配置（`src/driver/config.ts`、`xbintsc.config.json`）：`entry`、`outDir`、`output`、`optimize`、`lto`、`pgo`、`pgoDir`、`extensions`、`extNative`、`force` 与 `app`（`name`、`icon`、`bundle`、`bundleId`）。从入口/当前目录向上查找，可用 `--config` 指定、`--no-config` 关闭；所有路径以配置文件所在目录为基准；CLI 参数优先于配置。
- 应用图标（`src/driver/icon.ts`、`win-icon.ts`、`mac-bundle.ts`，`--icon`/`app.icon`）：PNG/ICO/ICNS 以 `xt_app_icon_*` 符号内嵌；Windows 上通过 `llvm-rc` 合成 PE 资源；macOS 上打包为 `Foo.app`（用 `sips`/`iconutil` 生成 `AppIcon.icns`）。`gui` 扩展用内嵌字节调用 `SDL_SetWindowIcon`；`xbintsc_RC` 可覆盖资源编译器。
- 编译器版本：`COMPILER_VERSION` 位于 `src/driver/version.ts`，是缓存键的一部分。pre-commit 钩子只在 `main`/`master` 上（与 `package.json` 一起）提升它，因此功能分支不会改动版本文件。
