# xbintsc

> 语言 / Language：**简体中文** | [English](./README.md)

`xbintsc` 把 **TypeScript 的一个实用子集直接编译为原生二进制**。
它自身解析 TypeScript，进行名字绑定，把程序下降为 **LLVM IR 文本**，
再把 IR 交给 **clang**，链接一个小型 C 运行时，产出独立可执行文件。

设计目标是：

1. **增量编译** —— 构建以内容寻址，没有变化时直接跳过。
2. **模块划分清晰** —— 词法、语法、绑定、代码生成、驱动、扩展各自独立目录，并有各自的测试。
3. **测试充分** —— 每个模块单元测试，外加真正编译并运行二进制的端到端测试。
4. **可插拔扩展** —— Node `fs`、`gui` HTML/CSS 渲染器 等是可选的编译模块，而非核心代码。
5. **自举（self-hosting）** —— 编译器可编译自身：`src/cli/main.ts` 能构建为原生 `xbintsc` 二进制，其产出的 LLVM IR 跨代达到不动点。CI 会为各平台构建这些二进制，Release 则附上这些产物。
6. **多平台** —— 在 x64 与 arm64（Apple Silicon / AArch64）上支持 macOS、Linux 与 Windows，并由 GitHub Actions 在各自架构的原生 runner 上验证。

## 工作原理

```
source.ts
   │  词法分析  src/lexer
   ▼
 tokens
   │  语法分析  src/parser        → AST (src/ast)
   ▼
 AST
   │  名字绑定  src/binder        → 作用域、符号、闭包
   ▼
 bound AST
   │  代码生成  src/codegen       → LLVM IR 文本 (src/codegen/llvm.ts)
   ▼
 module.ll ──clang──► module.o ──链接──► 可执行文件
                                  ▲
                          runtime/（C 运行时，NaN-boxing 值）
```

### 值模型

JavaScript 值统一为一个 64 位字（`xt_value`）。double 不做装箱直接存放；
其它类型是带标签指针：高 16 位 tag 加 48 位 payload。该表示法在
`src/codegen/values.ts` 与 `runtime/rt.h` 中定义一次，编译器与运行时共享。

### 调用约定

所有编译期函数使用统一 ABI：

```c
xt_value fn(xt_value env, int32_t argc, xt_value *argv);
```

`env` 通过 box 以引用方式线程化捕获变量，因此直接调用与闭包调用共用同一条
代码路径。不易内联的 JavaScript 语义（`+` 隐式转换、属性访问、打印）委托给
`@xt_*` 运行时调用。数值算术与比较是例外：两个操作数都是 double 时，codegen
直接内联硬件运算，只有需要隐式转换的情况才回退到运行时。

### 运行时

`runtime/` 实现字符串、对象、数组、闭包、算术、比较、异常与类 Node 的
`console.log` 打印。C 代码按功能拆分到多个翻译单元（`xt_alloc.c`、`xt_values.c`、
`xt_containers.c`、`xt_stdlib.c`、`xt_builtins.c`、`xt_io.c`），共享私有头
`runtime/rt_internal.h`。堆对象由 `xt_alloc` 分配，由非移动标记-清扫回收器回收：
根包括显式值槽（模块全局、原型缓存）、运行时根提供者（事件循环、Promise 微任务队列）
与对活动 C 栈的保守扫描。存活堆超过阈值后自动回收（`XT_GC_THRESHOLD` 可固定阈值），
也可按需触发；每次回收后阈值变为存活堆的两倍（下限 1 MiB），`XT_GC_TRACE=1` 会为
每次回收打印一行。错误对象会从生成代码维护的影子调用栈采集 JS 级堆栈（函数名、源文件
与函数级行号）；原生致命信号也会打印同一组帧。函数符号保留源码名称（如
`xt_fn_3_add`），原生调试器与崩溃报告同样可读。

## 使用方法

> **使用前请先确认：** 各平台的运行前置条件见
> [使用前置要求](./doc/zh-CN/requirements.md)。xbintsc 不再自带编译器，每个平台
> 都需要安装兼容 clang 的工具链（macOS：Xcode Command Line Tools；Linux：
> clang+lld；Windows：LLVM + Visual Studio C++ 生成工具）。

### 预编译独立发布包

每个 [GitHub Release](https://github.com/zy445566/xbintsc/releases/latest) 都会附上
各平台的发布归档，并在旁边附 `.sha256` 校验文件：

| 平台 | 归档 |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-<version>-win32-x64.tar.zst` / `xbintsc-<version>-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-<version>-linux-x64.tar.zst` / `xbintsc-<version>-linux-arm64.tar.zst` |
| macOS x64 / arm64（Apple Silicon） | `xbintsc-<version>-darwin-x64.tar.zst` / `xbintsc-<version>-darwin-arm64.tar.zst` |

（仅当构建机没有 `zstd` 时才会回退为 `.tar.gz`。）

`<version>` 是发布版本号（例如 `0.3.8`），会写入归档文件名，避免不同版本的下载文件相互覆盖。

每个归档解压后都是一个 `xbintsc-<os>-<arch>/` 目录，其中包含编译器
（`bin/xbintsc[.exe]`）与 C 运行时。它**无需 Node.js**，但你需要先安装对应平台
的工具链（见 [使用前置要求](./doc/zh-CN/requirements.md)）。解压后把 `bin/` 加入
`PATH`（或直接调用 `bin/xbintsc`）即可，xbintsc 本身无需安装步骤。

**macOS / Linux 示例** —— 此处以 `xbintsc-<version>-darwin-arm64.tar.zst` 为例（请按你的平台
换成 `-linux-x64` 等）：

```bash
# 1. 校验哈希（可选，但推荐）
shasum -a 256 -c xbintsc-<version>-darwin-arm64.tar.zst.sha256   # Linux：sha256sum -c

# 2. 解压
tar -xf xbintsc-<version>-darwin-arm64.tar.zst

# 3. 编译并运行 hello.ts（见下）
./xbintsc-darwin-arm64/bin/xbintsc run ./hello.ts

# 4. ...或产出独立的二进制
./xbintsc-darwin-arm64/bin/xbintsc build ./hello.ts --out ./build
./build/hello
```

**Windows 示例** —— 先安装 LLVM 与 Visual Studio C++ 生成工具（见
[使用前置要求](./doc/zh-CN/requirements.md)），再从
[Releases 页面](https://github.com/zy445566/xbintsc/releases/latest) 下载对应归档
（此处为 `xbintsc-<version>-win32-x64.tar.zst`；Windows on ARM 请用 `-arm64`），
然后在 **x64 Native Tools Command Prompt for VS 2022**（Windows on ARM 用
**ARM64 Native Tools**）中执行以下命令 —— Windows 10+ 自带 `tar`：

```powershell
# 1. 校验哈希（可选，但推荐）
(Get-FileHash .\xbintsc-<version>-win32-x64.tar.zst -Algorithm SHA256).Hash
Get-Content .\xbintsc-<version>-win32-x64.tar.zst.sha256

# 2. 解压
tar -xf .\xbintsc-<version>-win32-x64.tar.zst

# 3. 编译并运行 hello.ts（见下）
.\xbintsc-win32-x64\bin\xbintsc.exe run .\hello.ts

# 4. ...或产出独立的 hello.exe
.\xbintsc-win32-x64\bin\xbintsc.exe build .\hello.ts --out .\build
.\build\hello.exe
```

```ts
// hello.ts
console.log("Hello from xbintsc!");
```

提示：把 `xbintsc-win32-x64\bin` 加入 `PATH`，即可直接用 `xbintsc` 命令。

### 作为开发者（从源码）

在源码检出中，通过 `tsx` 直接运行 TypeScript 源码（或使用 `npm run xbintsc`）。
`bin` 启动器在这里同样可用：没有 `dist/` 构建时会自动回退到 `tsx`。

```bash
# 安装依赖
npm install

# 编译并运行程序
npm run xbintsc -- run examples/hello.ts
# 等价于
npx tsx src/cli/main.ts run examples/hello.ts

# 产出原生二进制
npx tsx src/cli/main.ts build examples/hello.ts --out build/examples
./build/examples/hello

# 查看生成的 LLVM IR
npx tsx src/cli/main.ts emit examples/hello.ts | head

# 使用可选扩展（这里为 Node 的 fs，通过 import 引入）
npx tsx src/cli/main.ts run examples/node/read.ts --ext node
```

CLI 选项：

```
-o, --output <path>   输出路径
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0..-O3              优化级别（默认 -O2）
    --ext <names>     逗号分隔扩展（如 node）
    --ext-native <m>  从 JSON manifest 注册 C++/Rust 扩展
    --force           忽略增量缓存
    --verbose         打印进度信息
    --time            打印各构建阶段耗时
```

### 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

## GUI 应用（HTML/CSS）

xbintsc 也能构建 **界面用 HTML/CSS 编写的原生桌面应用**。可选的 `gui` 扩展会把一个
自研的、GPU 加速的 HTML/CSS 渲染器（自有 HTML 解析器、CSS 层叠、布局与 SDL_GPU
合成器，不使用系统 WebView，也不内嵌浏览器）链接进可执行文件，并作为可 `import`
的模块暴露出来：

```ts
// app.ts
import { createWindow, run } from "gui";
// .html 资产加载器会把文档里的脚本提前编译（见下文）
import page from "./page.html";

const win = createWindow({ title: "Demo", width: 900, height: 600 });
win.on("click", (e: any) => console.log(e.x, e.y, e.target));
win.loadHTML(page);
run();                          // 主循环，直到所有窗口关闭
```

```bash
# 源码检出：先构建一次引擎归档（会获取并构建 SDL3、FreeType 与 HarfBuzz），
# 然后运行仓库自带示例
npm run gui
npx tsx src/cli/main.ts run examples/gui/hello.ts --ext gui

# 预编译的 Linux/macOS 发布包已经内含该引擎：
xbintsc run app.ts --ext gui
```

文档里的 `<script>` 脚本会被 xbintsc **提前编译（AOT）**进二进制——内联脚本
（裸 `<script>` 本身就按 TypeScript 处理）与外部 `<script src="./app.ts">` 文件
都是如此——因此运行时依然没有 JavaScript 引擎。元素句柄、DOM 变更、事件与脚本接口
见 [doc/zh-CN/gui-scripts.md](./doc/zh-CN/gui-scripts.md)。

在项目里把扩展写进 `xbintsc.config.json`（`"extensions": ["gui"]`）即可省去
`--ext gui`，与 `node` 完全一样。设置 `XT_GUI_AUTOCLOSE_MS=<ms>` 可在指定毫秒后
自动关闭所有窗口，端到端测试正是用它无头运行示例。

仓库里的示例是 [`examples/gui/pelican-bike`](./examples/gui/pelican-bike)
——一个可玩的 2D 游戏，它的同一份文档在浏览器里也能直接打开。

平台与完整说明：[doc/zh-CN/gui.md](./doc/zh-CN/gui.md) 是引擎总览（架构与
路线图）；按功能拆分的文档位于 [doc/zh-CN/gui/](./doc/zh-CN/gui) —— API、
DOM/CSS、布局、绘制、媒体、输入、动画、运行时、构建与已知问题。Linux 与 macOS 已在 CI 中构建
并实际运行，其发布包内含引擎（`gui.a`）；Windows 的 `gui.lib`（D3D12）由同一脚本产出，但仍是
暂定状态，尚未随发布包提供。

## 增量编译

驱动以入口源码哈希、编译器版本、编译选项、平台与启用的扩展集合作为每次构建的
键（`src/driver/cache.ts`）。如果所有记录的产物仍然存在，构建立即返回。C 运行时
与扩展源按同样的原则只编译一次并缓存。

## 扩展

扩展是一个普通对象（`src/extensions/registry.ts`）。内置扩展有两个：`gui`，
它贡献的是一个 GPU HTML/CSS 渲染器（见下文「GUI 应用（HTML/CSS）」一节与
[doc/zh-CN/gui.md](./doc/zh-CN/gui.md)）；以及 `node`，它本身按每个 Node
模块拆分目录，每个模块把它对外导出的绑定与实现它们的 C 源配对：

```
src/extensions/node/       runtime/ext_node/
  index.ts   # nodeExtension  fs/read_file.c   fs/write_file.c   fs/fs_ops.c
  fs/index.ts                path/path.c      os/os.c           process/process.c
  fs/read-file.ts
  path/index.ts
  os/index.ts
  process/index.ts
```

```ts
// src/extensions/node/index.ts
const modules: readonly NodeModule[] = [fsModule, pathModule, osModule, processModule];

export const nodeExtension: Extension = {
  name: "node",
  runtimeSources: () => [...new Set(modules.flatMap((m) => m.runtimeSources()))],
  modules: () => Object.fromEntries(
    modules.flatMap((m) => {
      const entry = { namespace: m.namespace, exports: m.exports?.() ?? m.builtins() };
      return [[m.name, entry], [`node:${m.name}`, entry]];
    }),
  ),
};
```

注册它会链接额外的 C 源，并通过 `import` 暴露 Node API：
`import { readFileSync } from "fs"` 会解析到具有统一 `(argc, argv)` 调用约定的 C
符号。`path`、`os` 与 `process` 还会接入命名空间分发，因此
`import path from "path"`（或 `import * as path from "path"`）会让 `path.join(...)`
与 `process.cwd()` 下降为各自的运行时入口。新增一个模块意味着在
`src/extensions/node/` 下放入一个目录、并在 `runtime/ext_node/` 下放入对应的 C
实现；核心编译器永不改动。

Node 模块覆盖情况：

- [Node 扩展：已实现](./doc/zh-CN/node-implemented.md)
- [Node 扩展：未实现](./doc/zh-CN/node-unimplemented.md)

### 原生扩展（C++ / Rust）

扩展并非只能用 C 或 TypeScript 编写。只要库以 `extern "C"` 暴露运行时 ABI 的
入口（`xt_value fn(int32_t argc, xt_value *argv)`），就可以被链接进来。把
C++ 或 Rust 代码编译为对象文件或静态库，用一个很小的 JSON manifest 描述它，
再通过 `--ext-native` 传入：

```bash
./examples/extensions/cpp/build.sh
xbintsc run examples/extensions/cpp/demo.ts \
  --ext-native examples/extensions/cpp/xbintsc.manifest.json
```

```jsonc
{
  "name": "mathx-cpp",
  "objects": ["build/mathx.o"],
  "linkerFlagsByPlatform": { "linux": ["-lstdc++", "-lm"], "darwin": ["-lc++"], "win32": ["-lmsvcprt"] },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },
  "modules": { "mathx": { "exports": { "add": { "symbol": "mathx_add" } } } }
}
```

之后 `import { add } from "mathx"` 会像 C 运行时绑定一样下降为 C++/Rust 符号。
可运行的 C++ 与 Rust 工程见 [`examples/extensions/`](./examples/extensions)，编写
辅助见 `runtime/xt_ext.h` / `runtime/xt_ext.rs`。

## 测试

```bash
npm run typecheck        # tsc --noEmit
npm test                 # 单元 + 端到端（存在 clang 时运行真实二进制）
npm run test:e2e         # 仅编译并运行的测试
npm run coverage         # TypeScript 编译器（src/）的 V8 覆盖率
npm run coverage:runtime # C runtime（runtime/）的 LLVM 覆盖率
```

测试在 `tests/` 下按模块组织（`lexer`、`parser`、`binder`、`codegen`、`driver`、
`extensions`、`cli`、`e2e`）。

V8 只能看到编译器自身的源码，因此 `npm run coverage:runtime` 改用 clang 的
源码插桩来覆盖 C runtime：脚本导出 `CCC_OVERRIDE_OPTIONS`（驱动执行的每次编译/链接
都会带上 `-fprofile-instr-generate -fcoverage-mapping`），运行测试后用
`llvm-cov` 把采集到的 profile 映射回 `runtime/`。加 `-- --report-only`
可只对上一次插桩运行留下的 profile 重新出报告，加 `-- --threshold 60`
可在低于覆盖率门槛时失败；CI 固定以 `--threshold 80` 运行，runtime 覆盖率回退会直接
让 job 失败。插桩产物存放在 `xbintsc_CACHE_DIR` 指向的独立缓存，
因为对象缓存的 key 不包含编译参数：与普通缓存共用会静默复用未插桩的对象。
脚本还会设置 `xbintsc_PREFER_PREBUILT=0`，强制从源码编译 runtime，而不是链接
未插桩的 `runtime/lib` 预编译归档。
需要与解析到的 clang 版本匹配的 `llvm-profdata`/`llvm-cov`；
缺失时脚本会打印警告并跳过。

## 运行要求

完整的分平台要求见 [使用前置要求](./doc/zh-CN/requirements.md)。若只使用预编译的
独立二进制，见
[使用前置要求](./doc/zh-CN/requirements.md)。

- Node.js 22+ —— 仅从源码运行/构建时需要；发布版二进制是独立的
- macOS：Xcode Command Line Tools（`xcode-select --install`）
- Windows：LLVM + Visual Studio C++ 生成工具（MSVC ABI）
- Linux：发行版提供的 `clang` 与 `lld`
- 若要自行编译二进制：`PATH` 中有兼容 `clang` 的 C 编译器（可用 `xbintsc_CLANG` 覆盖）

用 `xbintsc doctor` 检查环境。

## 语言子集

- [已实现特性](./doc/zh-CN/implemented.md)
- [未实现特性](./doc/zh-CN/unimplemented.md)

## 让 AI 使用 xbintsc

这个编译器对语言子集很严格——类型只被擦除而从不检查、Node 模块需要
`--ext node`、不支持直接 import 第三方 npm 包、字符串按 UTF-8 字节计、`async`
是同步微任务模型。如果 AI 把"TypeScript"当成"Node 能跑的 TypeScript"，写出来的
代码就编译不过，因此文档正是按这类读者分层组织的：

- [AGENTS.md](./AGENTS.md) —— **在本仓库内**工作的 AI 的入口：xbintsc 是什么、
  仓库结构、构建门禁与改动规则。
- [doc/ai/](./doc/ai) —— 面向使用者的任务导向指南，按任务拆分：
  [构建配方](./doc/ai/zh-CN/build-recipe.md)、
  [CLI 与配置](./doc/ai/zh-CN/cli.md)、
  [语言支持范围](./doc/ai/zh-CN/language-support.md)、
  [扩展](./doc/ai/zh-CN/extensions.md)、
  [排错](./doc/ai/zh-CN/troubleshooting.md)、
  [参与开发](./doc/ai/zh-CN/contributing.md)。只读当前任务需要的那一篇，不必通读。
  英文版：[doc/ai/](./doc/ai)。
- [llms.txt](./llms.txt) —— 所有文档的机器可读索引，让工具一次抓取即可，无需克隆
  仓库。

报错本身也会指路：编译器错误末尾会有一行 `hint: … — see <文档>`，指明解释该问题的
页面；需要干净输出时加 `--no-hints`。
