# xbintsc 应用图标（编译期图标嵌入）

> 语言：**简体中文** | [English](../icon.md)

状态：**已实现** —— 里程碑 I0–I3 已落地。I4（Linux 桌面集成）与 I5（按窗口图标）
仍为可选的后续项。

## 目标

让编译产物带上真实的系统级**应用 / 窗口图标**，图标在**编译期**提供并烘焙进产物，
运行时无需任何外置图标文件：

- **Windows** —— 图标显示在资源管理器、任务栏和 Alt-Tab（PE 资源）。
- **macOS** —— 图标显示在 Finder 和 Dock（带 `.icns` 的 `.app` bundle）。
- **Linux** —— 运行中的窗口/任务栏图标取自内嵌图片；桌面集成
  （`.desktop` + 主题 PNG）作为后续项。

核心编译器保持平台无关：词法/语法/绑定/代码生成永远不知道“图标”是什么。图标处理属于
*driver* 层（与已有的按平台链接参数同级），外加一个可选的 *runtime* 层（`gui` 扩展）。

## 用法

```bash
# CLI —— 图标是构建选项
xbintsc build app.ts --icon assets/app.png -o app
xbintsc run   app.ts --icon assets/app.png

# 编程接口
build("app.ts", { icon: "assets/app.png" });
```

图标设置——以及配套的应用元数据（名称、bundle id）——通常在**项目编译配置**
（`xbintsc.config.json`）中声明一次：

```json
{
  "$schema": "https://raw.githubusercontent.com/zy445566/xbintsc/main/doc/xbintsc.config.schema.json",
  "entry": "src/app.ts",
  "outDir": "build",
  "extensions": ["gui"],
  "app": {
    "name": "Demo",
    "icon": "assets/app.png",
    "bundle": true,
    "bundleId": "com.example.demo"
  }
}
```

```bash
xbintsc build                   # 读取 xbintsc.config.json
xbintsc build --icon other.png  # 命令行参数覆盖配置
```

```ts
// GUI 应用自动使用内嵌图标
import { createWindow, run } from "gui";
createWindow({ title: "Demo", width: 800, height: 600 });
run(); // 窗口/任务栏/Dock 图标 = 构建时配置的图标
```

## 第 0 层 —— 项目编译配置（`xbintsc.config.json`）

本功能引入（此前没有项目级编译配置）。一个签入的配置文件即可让构建可复现：
`xbintsc build` 无需任何参数。

### Schema

所有字段均可选：

```json
{
  "entry": "src/app.ts",
  "outDir": "build",
  "output": "build/Demo",
  "optimize": "2",
  "extensions": ["gui", "node"],
  "extNative": ["native/xbintsc.manifest.json"],
  "force": false,
  "app": {
    "name": "Demo",
    "icon": "assets/app.png",
    "bundle": true,
    "bundleId": "com.example.demo"
  }
}
```

- 严格 JSON（`JSON.parse`）；未知键被忽略。
- 所有相对路径以**配置文件所在目录**为基准解析，而非当前工作目录。
- `app.bundle` 仅 macOS 有效，控制是否生成 `.app` bundle。

### 发现与优先级

1. `--config <path>` 显式指定配置；`--no-config` 关闭读取。
2. 否则从入口文件所在目录向上、再到当前工作目录逐级查找
   `xbintsc.config.json`。
3. 每个选项的优先级：**CLI 参数 > 配置值 > 内置默认值**。
4. `xbintsc build` 未给入口位置参数时回退到配置的 `entry`；两者都没有则报错。

实现在 `src/driver/config.ts`（`loadProjectConfig`、`parseProjectConfig`、
`findProjectConfig`、`resolveConfigPaths`、`ProjectConfigError`），并在
`src/cli/main.ts` 中接线。

## 第 1 层 —— 嵌入与打包

### `src/driver/icon.ts`

```ts
export type IconFormat = "png" | "ico" | "icns";
export interface IconInfo {
  readonly path: string;
  readonly format: IconFormat;
  readonly bytes: Uint8Array;
  readonly width: number;  // 未知时为 0（如 .icns）
  readonly height: number;
}
export function readIcon(path: string): IconInfo;                  // 失败抛 IconError
export function iconSource(icon: IconInfo): string;                // 生成的 C
export function ensureIconObject(runner, clang, cacheDir, icon): string;
export function pngToIco(png: Uint8Array, width, height): Uint8Array;
export function toIcoBytes(icon: IconInfo): Uint8Array;
```

`readIcon` 通过魔数识别格式（PNG/ICO/ICNS），对缺失/为空/不支持的输入抛出
`IconError`，并在代价低时读取固有尺寸（PNG IHDR、ICO 目录项）。

### 内嵌符号

配置了图标时——以及每个 GUI 构建即使没有图标时——driver 会在缓存目录（绝不写入
用户源码树）生成一个小 C 文件，用现成的 `compileC` 编译，并把目标文件追加到 `link()`
的输入列表：

```c
/* 生成物，按图标内容哈希缓存 */
const unsigned char      xt_app_icon_data[];   /* 原始图标字节      */
const unsigned long long xt_app_icon_size;     /* 0 == 无图标       */
const char               xt_app_icon_format[]; /* "png" | "ico" | "icns" */
const unsigned int       xt_app_icon_width;
const unsigned int       xt_app_icon_height;
```

`gui` 扩展**无条件**引用这些符号，因此 driver 会为 GUI 程序链接一个空图标对象
（`EMPTY_ICON`）；`xt_app_icon_size == 0` 时运行时跳过它。如此避免了脆弱的
弱符号技巧（Mach-O 的 `weak` 不会像 ELF 那样解析为 null）。

### CLI 与 API 接线

- `src/cli/main.ts`：新增 `--icon`、`--bundle`、`--app-name`、`--app-id`、
  `--config`、`--no-config`，并与配置合并（`mergeAppConfig`）。
- `src/driver/compiler.ts`：`BuildOptions.icon` 与 `BuildOptions.app`。
  - 图标的**内容哈希**与打包选项计入可执行文件 `cacheKey`，图标变化即重建。
  - 图标对象以及（win32 上）资源对象追加到链接输入。
  - 链接后生成 macOS `.app` bundle；`BuildResult.bundlePath` 返回其路径
    （可执行文件路径不变，`run` 仍可用）。

### 各平台打包

#### Windows（PE 资源）—— `src/driver/win-icon.ts`

1. PNG 输入被包进最小 ICO 容器（`ICONDIR` + 指向 PNG 载荷的 `ICONDIRENTRY`）；
   Vista+ 接受 PNG-in-ICO。
2. 在缓存中写入 `.rc`（`1 ICON "icon-<hash>.ico"`）。
3. 优先 `llvm-rc /fo app.res app.rc`（解析出的 clang 同目录或 `PATH`），
   否则 MinGW `windres app.rc -O coff -o app_res.o`。可用 `xbintsc_RC` 覆盖。
4. 找不到资源编译器时继续构建（仅运行时图标），装饰性功能绝不导致构建硬失败。

#### macOS（`MyApp.app` bundle）—— `src/driver/mac-bundle.ts`

```
MyApp.app/Contents/
  Info.plist            CFBundleName/Identifier/Executable/IconFile
  MacOS/MyApp           链接产物副本
  Resources/AppIcon.icns
```

- `.icns` 直接拷贝；`.png` 用 `sips` + `iconutil` 转换。工具缺失时退化为随包携带
  `AppIcon.png`，运行时 Dock 图标仍然生效。
- `bundleId` 提供 `CFBundleIdentifier`，默认 `com.xbintsc.<binary>`。
- 打包是可选项（`app.bundle` / `--bundle`），因为它改变输出布局。

#### Linux

ELF 没有图标约定。唯一的内置用途是运行时窗口图标（取自内嵌字节）。后续可生成
`<name>.desktop` + `hicolor` PNG，并设置 `SDL_SetAppMetadata`/app-id 提示以适配
Wayland。

### 格式策略

- **PNG 是标准输入**，全平台可用。
- `.ico`（Windows）与 `.icns`（macOS）被接受：`.ico` 尽力作为 PE 资源，`.icns`
  原样拷贝进 mac bundle。

## 第 2 层 —— 运行时窗口/Dock 图标（`gui`）

`runtime/ext_gui/gui.cpp` 定义了 `xt_gui_apply_icon(SDL_Window *)`，在
`xt_gui_create_window` 创建窗口后调用：

- 读取 `xt_app_icon_{data,size,format}`；`size == 0` 时跳过。
- 用现成的 `xtgui::xt_image_decode`（stb_image）解码 PNG 字节。
- 构造 `SDL_Surface`（`SDL_CreateSurfaceFrom(..., SDL_PIXELFORMAT_RGBA32, pitch)`）
  并调用 `SDL_SetWindowIcon`。
- 任何失败都静默处理：图标是装饰性的，绝不能阻止程序启动。

## 缓存

- 可执行文件缓存键包含图标字节哈希、格式、应用名、bundle 开关与 bundle id。
- 生成的 C 对象按图标内容哈希缓存。
- Windows `.res`/COFF 对象按图标+编译器哈希缓存。
- `.app` bundle 属于缓存产物，不会复用过期 bundle。

## 测试

- `tests/driver/config.test.ts` —— 解析/校验、发现、路径解析。
- `tests/driver/icon.test.ts` —— 格式识别、生成的 C、ICO 转换、图标对象缓存、
  `llvm-rc`/`windres` 命令行、mac bundle 布局与退化路径，以及 `build()` 集成
  （链接图标对象；图标缺失 ⇒ `DiagnosticCode.IOError`）。
- `tests/e2e/icon.test.ts` —— 用 `app.icon` 编译真实 GUI 程序，断言可执行文件中
  存在 PNG 字节，并在无头模式运行；darwin 上校验 `.app` bundle 与 `.icns`。
- CLI 测试覆盖 `--config`/`--no-config`/`--icon`。

## 里程碑

0. **I0 —— 项目编译配置** ✅（`src/driver/config.ts`、CLI 接线、测试）。
1. **I1 —— 内嵌图标 + 运行时窗口图标** ✅（`--icon`、`src/driver/icon.ts`、
   `xt_app_icon_*`、`SDL_SetWindowIcon`、缓存键）。
2. **I2 —— Windows PE 资源** ✅（`src/driver/win-icon.ts`）。
3. **I3 —— macOS `.app` bundle** ✅（`src/driver/mac-bundle.ts`）。
4. **I4 —— Linux 桌面集成**（可选）。
5. **I5 —— 按窗口 / 按资源图标**（可选）。

## 决策

- **OQ-1 用法** —— 图标放在项目编译配置（`app.icon`），并以 `--icon` 覆盖。*已完成。*
- **OQ-2 macOS 打包** —— 显式 `--bundle` / `app.bundle`。*已完成。*
- **OQ-3 格式** —— PNG 必需；ICO/ICNS 尽力而为。*已完成。*
- **OQ-4 非 GUI 应用** —— 全平台支持（本质上就是一个资源）。*已完成。*
- **OQ-5 默认图标** —— 无；需显式启用。*已完成。*
- **OQ-6 配置格式/名称** —— `xbintsc.config.json`，严格 JSON（允许 `$schema`
  但被忽略）。*已完成。*
- **OQ-7 配置范围** —— 目前单个 `entry`。*已完成。*
