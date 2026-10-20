# 排错指南

先跑这条命令，看 xbintsc 到底解析到了什么：

```bash
xbintsc doctor
```

它会打印平台、将要使用的工具链（以及来源）、编译器版本、运行时目录、运行时库目录
与图标资源编译器。绝大多数"跑不起来"的答案都在这里。

## 首先确认：clang 够新吗？

**需要 clang/LLVM 16 或更新。** 更老的 clang 会以有类型指针（typed pointer）错误
拒绝生成的 IR：

```
D:\...\build\app.ll:382:37: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'
  %r0 = call i64 @xt_string_new(i8* @.str.0, i64 5)
```

编译器生成的 LLVM IR 依赖不透明指针（opaque pointers），而有类型指针在 LLVM 16
中被移除（[LLVM 发布说明](https://github.com/llvm/llvm-project/blob/release/15.x/llvm/docs/ReleaseNotes.rst#changes-to-the-llvm-ir)）。
`xbintsc build`/`run` 现在会先检查版本，直接停止并提示 "reports clang 15.0.2,
but xbintsc requires clang 16 or newer"；`xbintsc doctor` 会打印
`status : ok` 或 `status : too old`（后者以非零码退出）。搜索工具链时会优先选择受支持
的 clang——包括 Windows 上 Visual Studio 自带的 LLVM——所以装了新版时旧的
`/usr/bin/clang` 会被跳过。也可以显式指定：

```powershell
$env:xbintsc_CLANG = "C:\Program Files\LLVM\bin\clang.exe"
```

```bash
export xbintsc_CLANG=/usr/lib/llvm-18/bin/clang
```

注意：只做前端工作（`xbintsc emit`）完全不需要 clang，所以在旧工具链上它照常可用。

## 报错信息对照

### `module '…' is provided by the 'node' extension; pass --ext node`

程序导入了 Node 模块但扩展没开。加上参数，或写进 `xbintsc.config.json` 让它始终生效：

```bash
xbintsc run app.ts --ext node
```

### `module '<pkg>' is not supported: xbintsc can import built-in platform modules, relative '.ts' files and ESM packages under node_modules; CommonJS packages are not supported`

一个没人认领、也链不进来的裸第三方导入。要么该包是 CommonJS（不支持），要么它是
打包失败的 ESM 包。优先改用 `node` 扩展提供的 Node 内置模块；可打包的范围见
[extensions.md](./extensions.md)。

### ``CommonJS `require()` is not supported``

改写为 ESM：`const fs = require("fs")` 变成 `import fs from "fs"`（并加
`--ext node`）。`node_modules` 包内部的 `require` 由打包器处理；只有你自己的源码里
会被拒绝。`require(<非常量表达式>)` 永远不支持。

### `error TS4005: xbintsc does not yet support this <construct>`

语法解析通过了，但代码生成没实现。查 [language-support.md](./language-support.md) 与
[../../zh-CN/unimplemented.md](../../zh-CN/unimplemented.md)，然后改写该构造（或者
贡献实现）。常见的是 `namespace` 声明与 `new.target`。

### `error TS2xxx`（语法分析）或 `error TS1xxx`（词法分析）

源码语法错误。诊断会打印文件、行、列、出错的那一行以及插入符。注意 xbintsc 的
解析器接受绝大多数 TypeScript，所以语法错误通常意味着语法真的坏了，而不是"不支持"。

### `error TS6001: Cannot resolve module './x' from '<file>'` / `Cannot find module`

相对导入解析不到。`./helper.js` 说明符会映射到 `helper.ts`，所以要按你实际写的
TypeScript 运行时路径导入。

### `Cannot resolve module '…' required from '<file>'`

被打包的 `node_modules` 包内部的 `require(...)` 无法解析。该包不能直接用；请优先
使用内置模块或换一个依赖。

### `xbintsc: <config path>: invalid JSON (…)` / `Unable to read …`

项目配置或原生扩展清单格式错误或不存在。修好 JSON，或用 `--no-config` /
`--config <path>` 绕开自动发现。

### `error TS6003` / `Command failed (N): clang …`

clang 自身失败。抛出的消息包含完整的 clang 命令与 stderr——请读 stderr，不要只看
第一行。常见原因：

- 工具链太旧（见上文）；
- Windows 上 clang 因为环境没导入而找不到 MSVC/SDK 头文件或库。请在
  **x64 Native Tools Command Prompt for VS 2022**（ARM64 机器上是 **ARM64 Native
  Tools**）里运行，或先导入环境：
  ```powershell
  & "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
  ```
- Linux 上链接器不对——设置 `xbintsc_CLANG`，并/或传
  `xbintsc_LINKER_ARGS=-fuse-ld=lld`；
- `No C compiler found. Set xbintsc_CLANG to a clang binary.` —— `PATH` 里完全没有
  clang。

### `GUI native library not found at …`

`gui` 扩展需要各平台预编译的 `gui.a`/`gui.lib`，普通源码检出并没有构建它。去掉
`--ext gui`，或按 [../../zh-CN/gui.md](../../zh-CN/gui.md) 构建该归档。

### `Icon file not found` / 图标工具缺失

`--icon`/`app.icon` 的路径相对配置文件目录解析。Windows 上嵌入 PE 图标需要
`llvm-rc` 或 `windres`；`doctor` 会报告找到了哪个，只有在确实请求了图标时缺失才是
致命错误。

## 怎么看堆栈

`Error.stack` 由 codegen 采集，而不是依赖 C 运行时的原生帧：每个非生成器函数
在入口压入 `{名称, 文件, 行号}`，返回前弹出。帧形如

```
Error: boom
    at inner (app.ts:12)
    at module (app.ts:3)
```

行号取函数声明行；最内层帧会更新为 `new Error(...)` 所在行。没有列号；生成器
函数体因为会切换栈而不记录。原生致命信号会先打印 `xbintsc: fatal signal …`，
再打印同一组帧，所以即使没有调试器也能看出崩溃时正在运行哪些 JS 函数。

## 用 gdb / lldb / WinDbg 调试

`xbintsc build app.ts --debug` 会生成 LLVM 调试元数据，并把 `-g
-fno-omit-frame-pointer` 传给 clang，因此目标文件带有 DWARF（Linux/macOS）或
CodeView（Windows）行号表。函数符号保留源码名（如 `xt_fn_3_add`），每条语句都有
对应的行位置：

```bash
xbintsc build app.ts --debug
gdb ./build/app          # 或 lldb ./build/app，Windows 用 WinDbg 打开 .exe
```

调试版二进制更大，并且与普通构建分开缓存（debug 标志参与缓存键）。`Error.stack`
不需要 `--debug` 也能工作。

## 看起来不对、其实正常的构建行为

### 构建打印了 `(cached)` 并什么都没做

增量缓存命中了：源码哈希、编译器版本、选项、平台与扩展集合都没变，且所有产物都还在。
这正是设计目标。要强制重建就用 `--force`。

### 改了参数后产物是旧的

对象缓存的键刻意不含编译参数，这正是那些会改参数的脚本（如运行时覆盖率）使用独立
`xbintsc_CACHE_DIR` 的原因。如果怀疑产物过期，传 `--force` 或删掉 `.xbintsc/`。

### `run` 返回非零但没有错误信息

`run` 透传的是**被运行程序**的退出码，编译器本身成功了。直接跑 `./build/app` 看程序
自己的输出。

### `xbintsc: run requires --emit exe`

`run` 只能执行可执行文件。要看 IR 请用 `build --emit ir` 或 `emit`。

### 程序输出顺序出乎意料

`async`/`await` 只在 **pending** Promise 上挂起；等待已 settle 的值会同步继续，而 Node
会排入微任务，因此顺序可能不同。事件循环仍在程序主体之后启动，定时器与套接字回调依然
在主体之后触发；见 [language-support.md](./language-support.md)。

## 该去源码哪里看

| 症状 | 源码 |
| --- | --- |
| 某条诊断文本或代码 | [../../../src/diagnostics/diagnostic.ts](../../../src/diagnostics/diagnostic.ts)，以及 `src/` 下的发出点 |
| 某个特性缺失 | [../../../src/codegen/](../../../src/codegen/) —— `UnsupportedFeature` 在那里抛出 |
| 导入解析不了 | [../../../src/driver/bundler/](../../../src/driver/bundler/) |
| clang 调用、链接参数 | [../../../src/driver/toolchain.ts](../../../src/driver/toolchain.ts)、[../../../src/driver/toolchain-provider.ts](../../../src/driver/toolchain-provider.ts) |
| 缓存行为 | [../../../src/driver/cache.ts](../../../src/driver/cache.ts) |
| 运行时崩溃、GC、值 | `runtime/*.c`、`runtime/rt.h` |

如果你要改的是编译器本身而不是用它，接着读 [contributing.md](./contributing.md)。
