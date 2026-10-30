# 通过 npm 包使用 xbintsc

> 语言 / Language：[English](../npm-package.md) | **简体中文**

`xbintsc` 已经发布到 npm。你可以在任意 Node 项目里安装它，把这个项目的
TypeScript 直接编译成独立的原生二进制——不需要源码检出，不需要下载发布压缩包，
也不需要全局安装。

本文是面向 npm 包使用者的指南。编译参数与配置字段见
[CLI 参考](../ai/zh-CN/cli.md)；各平台的工具链要求见[使用前置要求](./requirements.md)。

## 快速开始

```bash
npm install --save-dev --save-exact xbintsc
npx xbintsc doctor                  # "status : ok" 表示 clang 可用
npx xbintsc build src/main.ts --out build
./build/main                        # Windows 上是 .\build\main.exe
```

后文会解释环境要求、首次构建成本、CI 用法以及与普通 Node 构建的差异。

## 给 AI 代理的说明

如果你是被要求构建一个已经依赖 `xbintsc` 的项目的 AI 代理：

1. 运行 `npx xbintsc doctor`。如果退出码非零，报告工具链问题；不要为了绕过
   缺失或过旧的 clang 去修改程序。
2. 找入口文件：`xbintsc.config.json` 里的 `entry`、调用 `xbintsc` 的 package
   脚本，或项目的主 TypeScript 文件（通常是 `src/main.ts`）。有配置文件时，
   `npx xbintsc build` 不需要位置参数。
3. 用 `npx xbintsc build <entry> --out build` 构建。如果程序导入了 Node 内置
   模块，加 `--ext node`；诊断信息会指出缺失的扩展。
4. 运行产物（`./build/<name>`，Windows 上是 `.exe`），并报告其路径。
5. 如果构建报不支持的语法，阅读安装包里的子集指南
   `node_modules/xbintsc/doc/ai/language-support.md`；不要假设 Node 能跑的
   TypeScript 就一定能编译。装有可选平台包时，首次构建直接链接预编译运行时；
   没有平台包时才会从源码编译 C 运行时，那次构建会明显更慢。

不要在 `node_modules/xbintsc` 里运行 `npm install`，也不要用 `tsc`/`esbuild`
代替 xbintsc：原生产物必须由 `npx xbintsc` 生成。`xbintsc` 不读取
`tsconfig.json`；请使用相对导入或包导入。

## npm 包里有什么

这个包包含：

- 编译器 CLI（`bin/xbintsc.js` 以及 `dist/` 里编译后的 JavaScript）；
- C 运行时**源码**（`runtime/`），其中包括 `node` 扩展的源码；
- 项目配置 schema 与文档。

它通过 Node.js 运行编译器。产出的二进制仍然是独立的：不嵌入 Node、V8 或
TypeScript 编译器。

安装它时还会安装宿主平台对应的可选包 `@xbintsc/<os>-<arch>`（例如
`@xbintsc/linux-x64`）。该包在 `runtime/lib/<os>-<arch>/` 下携带预编译运行时归档：
`core`、`ext_node`，以及发布时为该平台构建的 `gui` 与 `media`。驱动会就地链接这些
归档，因此首次构建不再编译 C 运行时，也不需要用户再去发布页下载。

这个包**不包含**：

- C 编译器——构建机器需要 clang 16+。

如果 npm 跳过了这个可选包（`--omit=optional`、不安装可选依赖的包管理器，或发布
未覆盖的宿主平台），驱动会回退到从随包源码编译 C 运行时并缓存目标文件；这样仍能
工作，但首次构建更慢，`--ext gui`（以及自动选择的 `media`/`gpu`/`images`/`shaping`/`webgl` 链接特性）也需要
归档存在。

## 环境要求

| 要求 | 原因 |
| --- | --- |
| Node.js **22+** | 用来运行编译器本身。产出的二进制不需要 Node。 |
| clang / LLVM **16+** | xbintsc 生成 LLVM IR，再交给 clang 编译和链接。 |
| 平台链接器 / SDK | macOS：Xcode Command Line Tools。Linux：clang + lld。Windows：LLVM + Visual Studio C++ 生成工具（MSVC ABI）。 |

首次构建前先确认：

```bash
node --version          # v22.x 或更高
npx xbintsc doctor      # "status : ok" 表示工具链可用
```

`doctor` 还会报告解析到的编译器、运行时目录和运行时库目录。Windows 上，如果
解析到的 clang 自己找不到 MSVC/SDK 头文件，请在 **x64 Native Tools Command
Prompt for VS 2022**（Windows on ARM 用 **ARM64 Native Tools**）中运行构建。
如果发行版默认的 `clang` 低于 16（例如 Ubuntu 22.04 或 Debian 12），请从该
发行版的 LLVM 软件源安装更新的 LLVM，并用 `xbintsc_CLANG` 指向那个二进制。
完整的分平台说明见[使用前置要求](./requirements.md)。

## 在项目里安装

在将要执行构建的 Node 项目里，把它装成开发依赖：

```bash
npm install --save-dev --save-exact xbintsc
```

`--save-exact` 会把编译器版本固定到 `package.json`；lockfile 则会为其他人和 CI
锁定同一版本。固定版本很重要：编译器版本是增量缓存 key 的一部分，而且新版本
可能产出不同的二进制。

其他包管理器的等价命令是 `pnpm add -D xbintsc` 和 `yarn add -D xbintsc`。
本文其余部分使用 npm 与 `npx`。

验证本地安装：

```bash
npx xbintsc version
npx xbintsc doctor
```

`npx` 会优先使用 `node_modules/.bin/xbintsc`。如果项目没有本地安装，`npx`
可能会转而下载最新版本——偶尔用一次很方便，但不可复现。在 CI 中，要么先安装
依赖，要么使用 `npx --no-install xbintsc …`，让缺失安装直接失败，而不是静默
拉取另一个版本的编译器。

### 不添加依赖的一次性运行

```bash
npx --yes xbintsc@latest build src/main.ts --out build
```

如果产物必须可复现，请把 `latest` 换成确切版本。这个命令会把包下载到 npm 的
`_npx` 缓存，所以常规构建还是推荐使用项目依赖。

### 全局安装（不推荐）

```bash
npm install --global xbintsc
xbintsc build src/main.ts --out build
```

全局安装适合临时试验，但所有项目会共享当前全局的那一个版本。项目级开发依赖
才能保证构建可复现。

## 一个完整的最小项目

```text
my-app/
├── package.json
├── xbintsc.config.json
└── src/
    └── main.ts
```

`src/main.ts`：

```ts
const greeting: string = "Hello from xbintsc";
console.log(greeting);
```

`xbintsc.config.json`：

```json
{
  "entry": "src/main.ts",
  "outDir": "build"
}
```

在 `package.json` 里加一个构建脚本——npm 会把 `node_modules/.bin` 放进
`PATH`，所以脚本里可以直接调用 `xbintsc`：

```json
{
  "scripts": {
    "build:native": "xbintsc build"
  }
}
```

然后：

```bash
npm install --save-dev --save-exact xbintsc
npm run build:native
./build/main          # Windows 上是 .\build\main.exe
```

预期输出：

```text
xbintsc: wrote /absolute/path/my-app/build/main
Hello from xbintsc
```

`xbintsc build` 不带位置参数时会从 `xbintsc.config.json` 读取 `entry`，所以脚本
可以写得很短。没有配置文件时，显式传入文件：`xbintsc build src/main.ts --out build`。

## 首次构建与缓存

装有平台包时，首次构建直接链接预编译的
`runtime/lib/<os>-<arch>/core.a`（Windows 上是 `.lib`），完全不编译 C，耗时与
增量构建接近。

如果可选包缺失——`npm install --omit=optional`、包管理器不安装可选依赖，或发布
未覆盖当前宿主平台——驱动才会从随包源码编译 C 运行时。运行时目标文件按源码/
头文件内容哈希缓存。默认缓存目录是**执行命令时所在目录**下的 `.xbintsc/`
（不一定是配置文件所在目录）；可以用 `xbintsc_CACHE_DIR` 改到别处。这种回退构建
会明显比后续构建慢。

如果确实想忽略平台包归档、改从主包自带的源码编译 C 运行时（例如修改了
`runtime/`，或预编译归档与当前环境不匹配），可以设置：

```bash
xbintsc_PREFER_PREBUILT=0 npx xbintsc build src/main.ts --out build
```

目标文件仍会缓存到 `.xbintsc/`；要完整重编，请把 `xbintsc_CACHE_DIR` 指向空目录
（或删除 `.xbintsc/`）。只加 `--force` 不会绕过预编译归档。归档文件本身由源码
检出中的 `npm run runtime` 生成；普通用户编译时不需要重新生成它们。

`--time` 可以看到时间花在哪里：

```bash
npx xbintsc build src/main.ts --out build --time
# 未装平台包时的回退构建：
# xbintsc: timing (frontend 4ms, codegen 7ms, clang 1089ms, runtime 37770ms, link 1966ms)
```

之后的构建会复用目标文件和增量构建记录：

```bash
npx xbintsc build src/main.ts --out build
# xbintsc: wrote /absolute/path/build/main (cached)
```

实际使用中的建议：

- 把 `.xbintsc/` 加进 `.gitignore`；
- 在 CI 里保留 `.xbintsc/`（或把 `xbintsc_CACHE_DIR` 指向持久目录），避免回退
  构建每次都重新编译运行时；
- 删除 `.xbintsc/` 是安全的——下一次构建会重新生成；
- `--force` 只忽略构建新鲜度记录，已缓存且内容哈希相同的运行时目标文件仍会复用。
  如果要强制完整重编 C 运行时，请删除 `.xbintsc/`。

## 常用命令

以下命令都假设已经安装了项目依赖，因此 `npx` 能找到本地包。

| 任务 | 命令 |
| --- | --- |
| 构建可执行文件 | `npx xbintsc build src/main.ts --out build` |
| 指定确切的输出路径 | `npx xbintsc build src/main.ts -o dist/app` |
| 编译后立即运行 | `npx xbintsc run src/main.ts -- --flag value` |
| 给程序传参数 | 放在 `--` 之后；它们不是编译器参数 |
| 只检查、不链接（不需要 clang） | `npx xbintsc check src/main.ts` |
| 打印 LLVM IR（不需要 clang） | `npx xbintsc emit src/main.ts > main.ll` |
| 只产出目标文件 | `npx xbintsc build src/main.ts --emit obj --out build` |
| 优化 | `npx xbintsc build src/main.ts -O3 --out build` |
| 使用 Node 内置模块 | `npx xbintsc build src/main.ts --ext node --out build` |
| 监听变化并重新构建 | `npx xbintsc watch src/main.ts` |
| 查看工具链 | `npx xbintsc doctor` |
| 查看完整帮助 | `npx xbintsc help` |

`--out <dir>` 是目录：输出文件名沿用入口文件的基本名。`-o <path>` 是确切的输出
路径。优化级别默认是 `-O2`。

完整参数、项目配置字段和环境变量见 [CLI 参考](../ai/zh-CN/cli.md)。

## 编译产物里的 Node 内置模块与 npm 依赖

从 npm 安装 `xbintsc` **并不**意味着编译产物运行在 Node 上。产物是独立的原生
二进制，因此有两条规则：

1. **Node 内置模块需要显式开启。** `node:fs` 或 `fs` 这样的导入需要 `node` 扩展：

   ```bash
   npx xbintsc build src/main.ts --ext node --out build
   ```

   或者永久写进 `xbintsc.config.json`：

   ```json
   {
     "entry": "src/main.ts",
     "outDir": "build",
     "extensions": ["node"]
   }
   ```

2. **第三方 npm 包在构建期打包。** 裸导入会从项目的 `node_modules` 解析，并把
   源码合并进二进制。运行时没有包查找。ESM 和 CommonJS 包都支持；循环依赖、
   动态 `require(<表达式>)`，以及依赖 `__dirname` 等 Node 全局变量的包不支持。

   ```bash
   npm install ms
   ```

   ```ts
   import ms from "ms";
   console.log(ms(60000));
   ```

完整规则与限制见[扩展](../ai/zh-CN/extensions.md#第三方-npm-包)。

## 在 Node 构建脚本里调用

npm 包只提供 ESM，并暴露了编程 API；当另一个 Node 构建脚本需要产出二进制时
很方便：

```js
// scripts/build-native.mjs
import { build } from "xbintsc";

const result = build("src/main.ts", { outDir: "build", optimize: "2" });
if (result.diagnostics.some((diagnostic) => diagnostic.category === "error")) {
  process.exitCode = 1;
} else {
  console.log(`wrote ${result.outputPath}${result.cached ? " (cached)" : ""}`);
}
```

用 `node scripts/build-native.mjs`（或 `package.json` 脚本）运行。注意：

- 使用 `.mjs` 或 `"type": "module"`，因为 `require("xbintsc")` 没有导出，会以
  `ERR_PACKAGE_PATH_NOT_EXPORTED` 失败；
- `build` 返回 `{ outputPath, irPath?, cached, diagnostics, … }`，源码错误通过
  `diagnostics` 报告；clang 自身失败时会**抛出** `ToolchainError`；
- `compileString(source)` 是纯 IR 生成调用，不需要 clang；
- 子路径 `xbintsc/driver` 暴露驱动内部能力（缓存、工具链、配置、图标、macOS
  bundle 助手）。

完整 API 见 [CLI 参考](../ai/zh-CN/cli.md#编程-api)。

## 在 CI 中使用

下面是一个发布 Linux 二进制的 GitHub Actions 示例：

```yaml
name: native
on: [push]
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 22
          cache: npm
      - run: sudo apt-get update && sudo apt-get install -y clang lld
      - run: npm ci
      - run: npx xbintsc doctor
      - uses: actions/cache@v4
        with:
          path: .xbintsc
          key: xbintsc-${{ runner.os }}-${{ hashFiles('package-lock.json') }}
      - run: npx xbintsc build src/main.ts --out build
      - uses: actions/upload-artifact@v4
        with:
          name: my-app-linux-x64
          path: build/main
```

- `npm ci` 默认安装 devDependencies，所以 `xbintsc` 可用。如果流水线使用
  `npm ci --omit=dev`，请在裁剪依赖前完成构建，或把 `xbintsc` 放进
  `dependencies`。
- `.xbintsc` 的缓存 key 包含 lockfile，而 lockfile 固定了编译器版本；缓存里的
  目标文件名是内容哈希，因此旧缓存不会污染新构建。
- xbintsc 只为**宿主**平台构建，不是交叉编译器：要给多个平台产出产物，请使用
  CI matrix（每个 OS/arch 一个 job）。
- macOS runner 已经带有 Xcode Command Line Tools；Windows runner 需要安装
  Visual Studio C++ 生成工具（MSVC + 自带的 LLVM），产物是 `build\main.exe`。
  在源码检出中可运行 `pwsh -File scripts/setup-vs-toolchain.ps1` 显式导入该
  工具链。

## npm 包还是发布压缩包？

| | npm 包（`xbintsc`） | 预编译发布压缩包 |
| --- | --- | --- |
| 需要 Node.js | 是，22+ | 否 |
| 首次构建 | 链接可选平台包里的预编译运行时 | 直接链接预编译运行时 |
| GUI 引擎 + media 归档 | Linux/macOS 与 Windows x64（发布为这些平台构建）包含 | Linux/macOS 与 Windows x64 压缩包包含 |
| 安装方式 | `npm install` + lockfile / provenance | 下载、校验 `.sha256`、解压 |
| 编程 API | 有 | 无（仅 CLI） |
| 交叉编译 | 否——只构建宿主平台 | 否——只构建宿主平台 |
| 适合 | Node 项目与 CI，沿用熟悉的 npm 流程 | 没有 Node 的机器、GUI 应用、完全自包含的安装 |

npm 包通过可选平台包获得同一套预编译运行时，只有在跳过该包时才会付出从源码编译
的冷启动成本。没有 Node 时请使用发布压缩包。

## 与普通 Node 构建的差异

- **不会读取 `tsconfig.json`。** xbintsc 使用自己的 `xbintsc.config.json` 指定
  入口、输出目录和构建参数。TypeScript 的 `paths` 别名以及其他 `tsconfig.json`
  设置不会生效；请使用相对导入或真实的包导入。
- **项目的 `package.json` `"type"` 不决定源码方言。** 写 ESM 的
  `import`/`export`；自己源码里的 `require()` 会被拒绝。
- **类型会先检查、再擦除。** 默认检查器能发现确定的类型不匹配，但没有完整的
  类型推断与 strict-null 模式。只有确实需要跳过语义检查时才使用 `--no-check`。
- **只支持 TypeScript 的一个子集。** 不支持的语法会给出诊断并失败，而不是被
  近似实现。见[语言支持范围](../ai/zh-CN/language-support.md)。
- **产物与平台、架构绑定。** 请在最终运行它的平台上构建。

## 排错

诊断信息末尾的提示使用仓库相对路径，例如 `doc/ai/troubleshooting.md`。如果项目是
通过 npm 安装的 xbintsc，请到 `node_modules/xbintsc/doc/ai/` 下读同一个文件。

| 现象 | 原因 / 处理 |
| --- | --- |
| `npx xbintsc` 用了意料之外的版本 | 运行 `npx --no-install xbintsc version` 和 `npm ls xbintsc`；在 `package.json` 中固定版本，CI 用 `npm ci`。 |
| `requires Node.js 22` / engine 错误 | 升级 Node；编译器使用了较新的 Node API。 |
| `No C compiler found` 或 `requires clang 16 or newer` | 安装 clang 16+，运行 `npx xbintsc doctor`；需要时用 `xbintsc_CLANG` 指定 clang。 |
| 第一次构建很慢 | 可选包 `@xbintsc/<os>-<arch>` 被跳过（或该宿主没有预编译包），正在编译并缓存运行时 C 源码。用 `npm ls @xbintsc/<os>-<arch>` 检查，并去掉 `--omit=optional` 重新安装；`--time` 可确认。 |
| 写 `.xbintsc/` 权限错误 | 用 `xbintsc_CACHE_DIR` 指向可写目录，或在可写目录中运行。 |
| `module 'fs' is provided by the 'node' extension` | 加 `--ext node`，或在配置里写 `"extensions": ["node"]`。 |
| `GUI native library not found` / `media native library not found` | 携带 `gui.a`/`gui.lib` 或 `media.a`/`media.lib` 的可选平台包被跳过，或该宿主没有预编译的 GUI/media 归档（Windows arm64）。去掉 `--omit=optional` 重新安装，或使用 Linux/macOS/Windows x64 发布压缩包，或从源码检出构建。自动检测到的 `media` 特性缺归档只会 warning；`--feature media` 才会报错。 |
| `require("xbintsc")` 失败 | 包只提供 ESM；使用 `import`（或动态 `import()`）。 |
| Windows 链接时报缺少 MSVC 库 | 在 Native Tools 命令提示符中运行，安装 VS C++ 生成工具，并检查 `npx xbintsc doctor`。 |
| `npm ci --omit=dev` 后构建找不到 `xbintsc` | 在构建阶段安装 devDependencies，或把 `xbintsc` 移到 `dependencies`。 |

更多错误信息与修复方式见[排错](../ai/zh-CN/troubleshooting.md)。
