# 参与编译器开发

本页接在 [../../AGENTS.md](../../../AGENTS.md) 之后读：那里讲了目录结构、构建门禁与规则，
这里讲容易搞错的工作流细节。

## 准备环境

```bash
npm install
npm run typecheck        # tsc --noEmit
npm run lint             # eslint + 600 行文件预算
npm test                 # 单元测试 + 端到端测试
```

从源码运行编译器需要 Node.js ≥ 22；任何会链接二进制的操作都需要 **clang 16+** 工具链
（见 [troubleshooting.md](./troubleshooting.md)）。`xbintsc emit` 既不需要 clang 也不需要
运行时库，因此它是最快的内循环。

可选：安装仓库钩子 `git config core.hooksPath .githooks`。它只在
`main`/`master` 上把补丁版本号 +1（同时更新 `package.json`、
`package-lock.json` 与 `src/driver/version.ts`）。功能分支从不改动这些文件，
这正是两个功能分支不会在版本行上冲突的原因；不要在功能分支上改版本号。
发布标签由 `release.yml` 创建，钩子从不打标签。

## 测试套件

| 命令 | 范围 |
| --- | --- |
| `npm test` | 全部：按模块的单元测试 + `tests/e2e` |
| `npm run test:e2e` | 只跑"编译并运行"的测试 |
| `npm run test:watch` | vitest 监听模式 |
| `npm run coverage` | TypeScript 编译器（`src/`）的 V8 覆盖率 |
| `npm run coverage:runtime` | C 运行时（`runtime/`）的 LLVM 覆盖率 |
| `npm run bench` | 构建并计时固定基准套件（可与 Node 对比） |

单元测试在 `tests/` 下按模块一个目录（`lexer`、`parser`、`binder`、`checker`、
`codegen`、`driver`、`extensions`、`cli`、`conformance`、`e2e`），与源码树镜像对应。
边界用例放在它覆盖的模块旁边。

`tests/e2e/` 会编译真实程序并运行产出的二进制，其中包含一个**差分测试框架**
（`tests/e2e/differential-*.test.ts`）：把同一份源码分别交给 xbintsc 和 Node 执行，
逐字节比较输出。这是仓库里最强的工具：只要动到语义，就往那里加用例，而不是断言一个
手写的期望字符串。需要 clang 的测试在 clang 缺失时会自动跳过
（[tests/helpers.ts](../../../tests/helpers.ts) 里的 `hasClang()`）——不要把这种跳过
变成静默通过。

`tests/conformance/` 是“支持子集”的可执行定义。功能可用时加一个受支持用例
（编译后与 Node 对比输出），记录边界时加一个不支持用例（预期诊断码）；
然后运行 `npm run docs:matrix` 重新生成[支持矩阵](../../support-matrix.md)，
提交的表格过期时测试会失败。

### 保持测试跨平台

测试套件在 Linux、macOS 和 Windows（x64 与 arm64）上原生运行，因此在一个平台通过的
用例必须在所有平台通过。三条规则覆盖了编译器有意保留的全部差异：

- **文本统一 LF。** `.gitattributes` 保证所有文本文件以 LF 检出，
  `.editorconfig` 让编辑器默认写入 LF。读取仓库文件一律
  走 [tests/platform.ts](../../../tests/platform.ts) 的 `readTextFile()`——它也会
  归一化陈旧的 CRLF 检出；不要用裸的 `readFileSync(..., "utf8")` 去比对生成文档。
  `runProgram()` 也会归一化捕获到的 stdout/stderr，子进程带出的 CRLF 不会让比较失败；
  需要逐字节断言的用例传 `raw: true`（UTF-8 输出测试就是这么做的）。
- **路径要构造，不要手写。** 原生期望值用 `node:path`（`join`、`resolve`、
  `relative`）构造。编译器在其与宿主无关的产物（LLVM IR、缓存清单、指纹、
  `path` 模块）里统一输出 `/`；断言这类值时用 `toPosixPath()` 把临时目录或
  `fileURLToPath()` 的结果转成该形式。编译器侧的唯一来源是
  [src/canonical.ts](../../../src/canonical.ts)（`ARTIFACT_PATH_SEPARATOR`、
  `ARTIFACT_LINE_ENDING`、`artifactPath()`），运行时对应
  [runtime/rt.h](../../../runtime/rt.h) 的 `XT_ARTIFACT_PATH_SEPARATOR`——生成产物的代码
  不要直接写 `"/"` 或 `"\n"`。
- **原生输出保持原生。** 诊断按驱动解析到的路径打印，因此 Windows 上含 `\`；
  堆栈帧属于生成产物，统一用 `/`。两者都用结构性断言
  （`toContain("helper.ts:1:")`、`includes("at inner")`）或与 `node:path`
  构造的值比较，不要写死 `/` 或 `\`。

不要归一化任意程序输出：其中的反斜杠可能是转义序列、JSON，或程序有意打印的 Windows
路径。真正与平台相关的特性（盘符、`os.platform()`、定时器、字体）用
`it.runIf(...)`/`it.skipIf(...)` 显式跳过，而不是用大范围字符串替换掩盖。

`tests/platform.test.ts` 同时守住两端：`.gitattributes` 策略与工作区的 LF 状态。
Windows 检出报告 CRLF 时，重新归一化一次——**只对干净的工作区执行**，因为
`git checkout -- .` 会丢弃本地改动：

```bash
git stash         # 或先提交
git add --renormalize .
git checkout -- .
git stash pop
```

## 两条不能破坏的不变量

### 1. 自举不动点

CI（[.github/workflows/ci.yml](../../../.github/workflows/ci.yml) 的 `self-host` 任务）
用编译器编译自身，并要求各代生成的 IR 完全一致：

```
源码 --emit--> ref.ll
源码 --build--> gen1 二进制
gen1 --emit--> gen2.ll      # 必须等于 ref.ll
gen1 --build--> gen2 二进制
gen2 --emit--> gen3.ll      # 必须等于 ref.ll
```

由此推论：**任何改变生成 IR 的修改，都必须能被正在被修改的编译器自己复现**，并且 IR
必须逐字节确定——包括迭代顺序、生成的符号名与编号。绝不要把不确定性（未受控的
`Map`/`Set` 迭代顺序、时间戳、依赖文件系统顺序的东西）引入代码生成。

第二个推论是字符串模型：编译器自身源码文本在各代中必须含义一致。编译后的字符串以 UTF-8
存储，但索引按 UTF-16 码元进行，所以自举后的 `readFileSync`（`--ext node`）交给扫描器的是
字节串（一个码元一个字节，全部 `<= 0xFF`），而 Node 交给它的是同一个文件已解码后的文本——
读取源码文件一律走 `sourceTextBytes`（[src/diagnostics/source-text.ts](../../../src/diagnostics/source-text.ts)），
不要假设一个码元就是一个字节或一个字符。弄错的表现是：`gen2.ll` 里非 ASCII 字面量（比如提示语里的 `—`）
被重新编码，这也正是这里逐字节比较 IR 而不是逐行比较的原因。

本地验证：

```bash
npx tsx src/cli/main.ts emit src/cli/main.ts --ext node > scratch/ref.ll
npx tsx src/cli/main.ts build src/cli/main.ts --ext node --out scratch/self --force
./scratch/self/main emit src/cli/main.ts --ext node > scratch/gen2.ll   # Windows 上是 .exe
diff scratch/ref.ll scratch/gen2.ll
```

### 2. 运行时 ABI 与值模型

- 每个编译后的函数都用同一条 ABI：
  `xt_value fn(xt_value env, int32_t argc, xt_value *argv)` —— 直接调用与闭包调用共用
  这一条路径，`env` 通过 box 传递被捕获的变量。
- 每个值都是一个 64 位字：double 不装箱，其余都是带标签指针（16 位标签 + 48 位载荷）。
  该表示只定义一次：[../../../src/codegen/values.ts](../../../src/codegen/values.ts) 与
  [../../../runtime/rt.h](../../../runtime/rt.h)；要改就一起改，否则别动。
- 别扭的 JS 语义（`+` 强转、关系比较、属性访问、打印）委托给 `@xt_*` 运行时调用。
  数值算术与比较是例外：codegen 内联双 double 快路径
  （`src/codegen/generator/numeric.ts`），其余情况调用运行时。
- 字符串字面量会被驻留：每个不同字面量只在 `main` 中创建一次，存入 GC 根全局，
  每次使用只做一次 load；属性名因此不再逐次分配。
- GC 是非移动标记清扫：新堆对象走 `xt_alloc`；值存活期间必须能从显式根槽、已注册的
  根提供者（事件循环、微任务队列、异步 I/O job 根）或 C 栈保守扫描到达。只存在于未被扫描的 C 局部
  变量里的值就是 bug。

## 修改运行时

`runtime/` 是 C，按函数拆到多个编译单元（`xt_alloc.c`、`xt_values.c`、
`xt_containers.c`、`xt_stdlib.c`、`xt_stdlib2.c` 等），共享 `runtime/rt_internal.h`。
C 改动必须重建运行时才会生效：

```bash
npm run runtime            # tsx scripts/build-runtime.ts -> runtime/lib/<os>-<arch>/
npm run runtime:clean      # 删除 build/runtime-obj 与 runtime/lib
```

新增 C 文件时必须同时更新列出运行时源文件的地方（[../../../src/driver/runtime-objects.ts](../../../src/driver/runtime-objects.ts)
里的 `RUNTIME_SOURCES`），因为那份列表决定什么会被编译和链接。扩展的 C 源由扩展自己声明。

## 新增或扩展一个扩展

扩展就是一个普通对象（[../../../src/extensions/registry.ts](../../../src/extensions/registry.ts)），
贡献 `runtimeSources()`、`modules()` / `builtins()`，可选贡献 `nativeObjects()` 或
`assetLoaders()`。Node 扩展是"每个模块一个目录"，把 TypeScript 导出与实现它的 C 源
配成对：

```
src/extensions/node/fs/index.ts        runtime/ext_node/fs/read_file.c
```

因此新增一个 Node 模块就是在两处各放一个目录，并在模块列表里注册；核心编译器不改。
如果你的特性是可选的或与平台相关，它属于某个扩展，而不是 `src/codegen`。内置扩展
列在 [../../../src/extensions/catalog.ts](../../../src/extensions/catalog.ts) —— 列在那里
但未开启的扩展会产生可操作的 `pass --ext <name>` 提示，所以即使默认关闭也要把模块
加进列表。

不改编译器、纯用 C++/Rust 编写扩展的做法见
[../../../examples/extensions/README.md](../../../examples/extensions/README.md)。

## 新增一条诊断

1. 在 [../../../src/diagnostics/diagnostic.ts](../../../src/diagnostics/diagnostic.ts)
   的枚举里按阶段加入稳定代码（1xxx 词法、2xxx 语法、3xxx 绑定、4xxx 检查、5xxx 代码
   生成、6xxx 驱动）。诊断码是公开接口——绝不要重新编号。
2. 用你手上最精确的范围发出它；`formatDiagnostic` 会据此渲染文件、行、列、片段与
   插入符。
3. 让消息可操作，风格与现有消息一致：既说清哪里错了，也说清该做什么（`pass --ext
   node`、`use an ESM import` 等）。如果读者还需要更多信息，CLI 的提示层会把诊断码
   映射到文档——新增诊断码时同步扩展
   [../../../src/cli/main.ts](../../../src/cli/main.ts) 里的映射。
4. 在 `tests/diagnostics` 或该模块自己的测试里覆盖它。

## 文档是改动的一部分

文档没同步，功能就不算完成：

| 文档 | 何时更新 |
| --- | --- |
| [../../zh-CN/implemented/](../../zh-CN/implemented/) 中拥有该功能的页面 | 某特性开始可用 |
| [../../zh-CN/unimplemented/](../../zh-CN/unimplemented/) 中拥有该限制的页面 | 出现或解除了某个限制 |
| [../../zh-CN/implemented.md](../../zh-CN/implemented.md) / [../../zh-CN/unimplemented.md](../../zh-CN/unimplemented.md) | 新增整页时（在索引里加链接） |
| [../../zh-CN/implemented/node.md](../../zh-CN/implemented/node.md) / [../../zh-CN/unimplemented/node.md](../../zh-CN/unimplemented/node.md) | Node 模块覆盖变化 |
| [language-support.md](./language-support.md) | 上述内容的 AI 摘要变化 |
| [../../zh-CN/requirements.md](../../zh-CN/requirements.md) | 工具链或平台要求变化 |
| [cli.md](./cli.md) | 参数、配置字段或环境变量变化 |

每个功能域只拥有一个小页面，因此功能分支改自己领域的页面（以及它的中文翻译），
而不是一份大文档。索引页的「改动应该写在哪一页？」表把改动映射到页面。
`tests/docs/structure.test.ts` 会检查每个英文页面都有中文对应页，且都被索引链接。

中文翻译（`doc/zh-CN/`、`doc/ai/zh-CN/`）要与英文源同步；半翻译的页面比没有更糟，因为
它会静默过期。

## 仓库卫生

- **每个代码文件 600 行**：TS/JS 由 ESLint（`max-lines`）强制，C、C++、Rust、`.inc`
  与 shell 由 [scripts/check-file-length.ts](../../../scripts/check-file-length.ts) 强制。
  请按职责拆分而不是把文件撑大；vendored 源码豁免。
- **禁止 `any`**；`prefer-const` 与 `eqeqeq` 是错误。解析器/生成器里的声明合并模式是
  有意放行的。
- **公开 API 变更**走 [../../../src/index.ts](../../../src/index.ts)，并且必须保持
  [../../../package.json](../../../package.json) 里 `xbintsc` / `xbintsc/driver` 的导出
  映射有效。
- `npm run package-release` 会把各平台压缩包组装到 `dist/release/`；发布工作流负责打
  标签，所以永远不要手工推标签。
