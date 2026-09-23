# xbintsc

> 语言 / Language：**简体中文** | [English](./README.md)

`xbintsc` 把 **TypeScript 的一个实用子集直接编译为原生二进制**。
它自身解析 TypeScript，进行名字绑定，把程序下降为 **LLVM IR 文本**，
再把 IR 交给 **clang**，链接一个小型 C 运行时，产出独立可执行文件。

设计目标是：

1. **增量编译** —— 构建以内容寻址，没有变化时直接跳过。
2. **模块划分清晰** —— 词法、语法、绑定、代码生成、驱动、扩展各自独立目录，并有各自的测试。
3. **测试充分** —— 每个模块单元测试，外加真正编译并运行二进制的端到端测试。
4. **可插拔扩展** —— Node `fs`、Bun API 等是可选的编译模块，而非核心代码。
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
代码路径。不易内联的 JavaScript 语义（`+` 隐式转换、关系比较、属性访问、打印）
委托给 `@xt_*` 运行时调用。

### 运行时

`runtime/` 实现字符串、对象、数组、闭包、算术、比较、异常与类 Node 的
`console.log` 打印。C 代码按功能拆分到多个翻译单元（`xt_alloc.c`、`xt_values.c`、
`xt_containers.c`、`xt_stdlib.c`、`xt_builtins.c`、`xt_io.c`），共享私有头
`runtime/rt_internal.h`。它使用 bump arena 且永不释放 —— 垃圾回收被有意推迟并
隔离在 `xt_alloc` 之后，因此可以在不改动编译器的前提下替换。

## 使用方法

> **使用前请先确认：** 各平台的运行前置条件见
> [使用前置要求](./doc/zh-CN/requirements.md)。若使用下方的预编译独立发布包，
> 相关部分为
> [使用前置要求 → 预编译发布包](./doc/zh-CN/requirements.md#预编译发布包推荐)
> （Windows/Linux 自带工具链；macOS 需要 Xcode Command Line Tools）。

### 预编译独立发布包

每个 [GitHub Release](https://github.com/zy445566/xbintsc/releases/latest) 都会附上
各平台的自包含归档，并在旁边附 `.sha256` 校验文件：

| 平台 | 归档 |
| --- | --- |
| Windows x64 / arm64 | `xbintsc-win32-x64.tar.zst` / `xbintsc-win32-arm64.tar.zst` |
| Linux x64 / arm64 | `xbintsc-linux-x64.tar.zst` / `xbintsc-linux-arm64.tar.zst` |
| macOS x64 / arm64（Apple Silicon） | `xbintsc-darwin-x64.tar.zst` / `xbintsc-darwin-arm64.tar.zst` |

（仅当构建机没有 `zstd` 时才会回退为 `.tar.gz`。）

每个归档解压后都是一个 `xbintsc-<os>-<arch>/` 目录，其中已包含编译器
（`bin/xbintsc[.exe]`）、C 运行时与自带工具链，因此**无需 Node.js，也无需系统编译器**。
解压后把 `bin/` 加入 `PATH`（或直接调用 `bin/xbintsc`）即可，无需任何安装步骤。

**macOS / Linux 示例** —— 此处以 `xbintsc-darwin-arm64.tar.zst` 为例（请按你的平台
换成 `-linux-x64` 等）：

```bash
# 1. 校验哈希（可选，但推荐）
shasum -a 256 -c xbintsc-darwin-arm64.tar.zst.sha256   # Linux：sha256sum -c

# 2. 解压
tar -xf xbintsc-darwin-arm64.tar.zst

# 3. 编译并运行 hello.ts（见下）
./xbintsc-darwin-arm64/bin/xbintsc run ./hello.ts

# 4. ...或产出独立的二进制
./xbintsc-darwin-arm64/bin/xbintsc build ./hello.ts --out ./build
./build/hello
```

**Windows 示例** —— 从
[Releases 页面](https://github.com/zy445566/xbintsc/releases/latest) 下载对应归档
（此处为 `xbintsc-win32-x64.tar.zst`；Windows on ARM 请用 `-arm64`），
然后在 PowerShell 中执行（Windows 10+ 自带 `tar`）：

```powershell
# 1. 校验哈希（可选，但推荐）
(Get-FileHash .\xbintsc-win32-x64.tar.zst -Algorithm SHA256).Hash
Get-Content .\xbintsc-win32-x64.tar.zst.sha256

# 2. 解压
tar -xf .\xbintsc-win32-x64.tar.zst

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
npx tsx src/cli/main.ts run examples/read-file.ts --ext node
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
```

### 编程接口

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

## 增量编译

驱动以入口源码哈希、编译器版本、编译选项、平台与启用的扩展集合作为每次构建的
键（`src/driver/cache.ts`）。如果所有记录的产物仍然存在，构建立即返回。C 运行时
与扩展源按同样的原则只编译一次并缓存。

## 扩展

扩展是一个普通对象（`src/extensions/registry.ts`）。`node` 扩展本身按每个 Node
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
  "objects": ["build/libmathx.a"],
  "linkerFlagsByPlatform": { "linux": ["-lstdc++"], "darwin": ["-lc++"], "win32": ["-lc++", "-static"] },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },
  "modules": { "mathx": { "exports": { "add": { "symbol": "mathx_add" } } } }
}
```

之后 `import { add } from "mathx"` 会像 C 运行时绑定一样下降为 C++/Rust 符号。
可运行的 C++ 与 Rust 工程见 [`examples/extensions/`](./examples/extensions)，编写
辅助见 `runtime/xt_ext.h` / `runtime/xt_ext.rs`。

## 测试

```bash
npm run typecheck   # tsc --noEmit
npm test            # 单元 + 端到端（存在 clang 时运行真实二进制）
npm run test:e2e    # 仅编译并运行的测试
```

测试在 `tests/` 下按模块组织（`lexer`、`parser`、`binder`、`codegen`、`driver`、
`extensions`、`cli`、`e2e`）。

## 运行要求

完整的分平台要求见 [使用前置要求](./doc/zh-CN/requirements.md)。若只使用预编译的
独立二进制，相关部分为
[使用前置要求 → 预编译发布包](./doc/zh-CN/requirements.md#预编译发布包推荐)
（Windows/Linux 自带工具链；macOS 需要 Xcode Command Line Tools）。

- Node.js 22+ —— 仅从源码运行/构建时需要；发布版二进制是独立的
- macOS：Xcode Command Line Tools（`xcode-select --install`）
- Windows：无需额外安装 —— 自带 MinGW-w64 工具链
- Linux：系统 C 库（glibc）
- 若要自行编译二进制：`PATH` 中有兼容 `clang` 的 C 编译器（可用 `xbintsc_CLANG` 覆盖）

用 `xbintsc doctor` 检查环境。

## 语言子集

- [已实现特性](./doc/zh-CN/implemented.md)
- [未实现特性](./doc/zh-CN/unimplemented.md)
