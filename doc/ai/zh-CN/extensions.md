# 扩展：Node 模块、GUI、C++/Rust 库

编译器核心与平台无关。任何与平台相关的东西——Node 的 `fs`、一个 HTML/CSS 渲染器、
你自己的 C++ 库——都以**扩展**的形式接入：它既贡献模块绑定，也贡献要一起编译/链接的
C/C++ 源码或目标文件。

## 最容易踩的规则

**只要扩展没开启，import 它提供的模块就会失败。** xbintsc 知道该模块存在，并明确
告诉你要传什么：

```bash
$ xbintsc run app.ts            # app.ts: import { readFileSync } from "fs";
error TS6001: module 'fs' is provided by the 'node' extension; pass --ext node
```

```bash
xbintsc run app.ts --ext node
```

多个扩展用逗号分隔一次开启：`--ext node,gui`。在项目里写进
`xbintsc.config.json` 就不需要每次带参数：

```json
{ "entry": "src/app.ts", "outDir": "build", "extensions": ["node"] }
```

## 内置扩展

### `node` —— Node 内置模块

用 `--ext node` 开启。可以用裸名或 `node:` 前缀导入（`import { readFileSync } from
"node:fs"`），两种写法解析到同一实现。

| 模块 | 说明 |
| --- | --- |
| `fs` | **仅同步 API**（`readFileSync`、`writeFileSync` 等）；`fs/promises` 是独立模块 |
| `fs/promises` | 基于 Promise 的文件 API |
| `path` | 同时接入命名空间分发，`path.join(...)` 可用 |
| `os` | |
| `process` | `process.cwd()`、`argv`、`env` 等；也接入命名空间分发 |
| `buffer` | |
| `crypto` | |
| `stream`、`stream/promises` | |
| `events` | |
| `net`、`dgram`、`http` | 跑在 xbintsc 事件循环上的套接字与服务端（注意下面的异步限制） |
| `child_process` | |
| `worker_threads` | |
| `util`、`querystring`、`url`、`assert`、`test`、`zlib` | |

各模块的具体覆盖（确切函数与选项）见
[../../zh-CN/node-implemented.md](../../zh-CN/node-implemented.md)，缺失项见
[../../zh-CN/node-unimplemented.md](../../zh-CN/node-unimplemented.md)。

在 `net`/`http` 上搭东西之前，先记住运行时模型：事件循环在**程序主体之后**才运行，
而 `async`/`await` 是同步微任务模型。见 [language-support.md](./language-support.md)。

### `gui` —— HTML/CSS 窗口

用 `--ext gui` 开启并导入 `gui` 模块：

```ts
import { createWindow, run } from "gui";
```

它是自带渲染引擎的 GPU 加速实现（自己的 HTML 解析器、CSS 级联、布局与合成器），不是
系统 WebView。因为主体是 C++，它以各平台预编译的 `gui.a`/`gui.lib` 形式分发。如果该
平台的归档不存在，构建会以明确的提示失败；设计与路线图见 [../../zh-CN/gui.md](../../zh-CN/gui.md)，
脚本接口见 [../../zh-CN/gui-scripts.md](../../zh-CN/gui-scripts.md)。

## 第三方 npm 包

没有被已启用扩展认领的裸说明符，会在 `node_modules` 里查找并**作为源码**打包。不是
纯 ESM TypeScript/JavaScript 的包——或依赖 CommonJS、`require`、`__dirname`、循环
依赖的包——都不会工作。`require()` 会被拒绝并提示改用 `import`。

实践结论：写 Node 程序时，正确做法通常就是开 `--ext node`；只有确实必要时才去依赖
npm 包。

## 原生扩展（C++ / Rust，无需改编译器）

任何暴露了运行时 ABI 的 `extern "C"` 入口的代码都能被链接进来：

```c
xt_value my_fn(int32_t argc, xt_value *argv);
```

用**外部**工具链（clang++ 或 cargo）构建它，把产物写进一个 JSON 清单，然后传清单：

```bash
xbintsc build demo.ts --ext-native ./xbintsc.manifest.json
xbintsc build demo.ts --ext-native a.json,b.json      # 多个
```

```jsonc
{
  "name": "mathx-cpp",                       // 必填，唯一
  "objects": ["build/mathx.o"],              // .o / .a / .lib，相对本文件
  "linkerFlagsByPlatform": {                 // C++/Rust 运行时
    "linux":  ["-lstdc++", "-lm"],
    "darwin": ["-lc++"],
    "win32":  ["-lmsvcprt"]
  },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },   // 无需 import
  "modules": {
    "mathx": { "exports": { "add": { "symbol": "mathx_add" } } }
  }
}
```

之后 `import { add } from "mathx"` 会像 C 运行时绑定一样降级到原生符号；`builtins`
无需导入即可全局调用。

编写辅助头：`runtime/xt_ext.h`（C/C++）与 `runtime/xt_ext.rs`（Rust），它们封装了参数
与值的辅助函数。关键 ABI 事实：值是 64 位 NaN-boxed 字；字符串**不**以 NUL 结尾，
所以要把 `xt_string_data` 与 `xt_string_length_value` 配对使用；运行时交给你的内存
不会被移动或释放。请使用与 xbintsc 解析到的同一个 clang/ABI 构建（`xbintsc doctor`
会打印它）；在 Windows 上要在 **x64/ARM64 Native Tools 命令提示符**里构建。

可直接照抄的工程：[`examples/extensions/cpp`](../../../examples/extensions/cpp) 与
[`examples/extensions/rust`](../../../examples/extensions/rust)；完整指南见
[../../../examples/extensions/README.md](../../../examples/extensions/README.md)。

## 编程式注册

```ts
import { build, createDefaultRegistry, nativeExtensionFromManifest, nodeExtension } from "xbintsc";

const extensions = createDefaultRegistry()
  .register(nodeExtension)
  .register(nativeExtensionFromManifest("./xbintsc.manifest.json"));

build("demo.ts", { extensions });
```

注册表也是宿主工具提示"某扩展未开启"的机制，正是它把错误变成了可操作的
"pass `--ext node`"，而不是下游令人困惑的报错
（[../../../src/extensions/catalog.ts](../../../src/extensions/catalog.ts)）。内置扩展
列表由 `bundledExtensions()` 提供。
