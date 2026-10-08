# CLI 与配置参考

权威的 `--help` 文本就是 [../../src/cli/main.ts](../../../src/cli/main.ts) 里的 `HELP`
常量。本页补充帮助文本没有明说的语义。

## 子命令

| 命令 | 作用 | 需要 clang？ |
| --- | --- | --- |
| `xbintsc build <file.ts>` | 编译为 `exe`（默认）、`obj` 或 `ir` | 需要，`--emit ir` 除外 |
| `xbintsc run <file.ts> [-- args]` | 编译成可执行文件并运行 | 需要（且只支持 `--emit exe`） |
| `xbintsc emit <file.ts>` | 把 LLVM IR 打到 stdout，不写文件 | 不需要 |
| `xbintsc doctor` | 报告解析到的工具链、运行时与图标工具 | 仅在探测时需要 |
| `xbintsc version` | 打印版本 | 不需要 |
| `xbintsc help` | 打印帮助文本 | 不需要 |

任何命令加 `--help` 都会打印帮助并返回 0。未知命令会打印错误与帮助文本，返回 1。

## 参数

```
-o, --output <path>   显式输出路径（覆盖 --out 与配置）
    --out <dir>       输出目录（默认 build/）
    --emit <kind>     exe | obj | ir（默认 exe）
-O0 .. -O3            传给 clang 的优化级别（默认 -O2）
    --ext <names>     开启内置扩展，逗号分隔（如 node,gui）
    --ext-native <m>  通过 JSON 清单注册 C++/Rust 扩展（多个用逗号分隔）
    --config <path>   使用指定项目配置，不做自动发现
    --no-config       完全不读取项目配置
    --icon <path>     嵌入应用图标（PNG/ICO/ICNS）
    --bundle          macOS：额外产出 <name>.app
    --app-name <name> Bundle / 显示名
    --app-id <id>     macOS bundle 标识（如 com.example.demo）
    --force           忽略增量缓存
    --verbose         打印进度信息
```

少数容易踩的解析细节：

- `--flag=value` 与 `--flag value` 都支持。
- `-O`、`-O1`、`-O2`、`-O3` 都可以；裸 `-O` 等价于 `-O2`。
- `--` 结束参数解析：其后的内容在 `run` 下属于被运行程序，在其他命令下是位置参数。
- `-o` 会无条件吃掉下一个参数；其他带值参数在后一个参数以 `-` 开头时视为未给值。

## 项目配置

`xbintsc.config.json` 从**入口文件所在目录向上**查找（没有给入口文件时从当前目录
开始）。文件里的每个路径相对该配置文件解析，命令行参数覆盖对应字段。`--no-config`
完全跳过发现流程。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `entry` | string | 入口 TypeScript 文件，使 `xbintsc build` 无需位置参数 |
| `outDir` | string | 输出目录（默认 `build/`） |
| `output` | string | 显式输出路径，覆盖 `outDir` |
| `optimize` | `"0"｜"1"｜"2"｜"3"` | 优化级别 |
| `extensions` | string[] | 开启的内置扩展，如 `["node"]` |
| `extNative` | string[] | 原生扩展清单路径 |
| `force` | boolean | 忽略增量缓存 |
| `app.name` / `app.icon` / `app.bundle` / `app.bundleId` | | 应用元信息（[icon.md](../../zh-CN/icon.md)） |

JSON schema：[xbintsc.config.schema.json](../../xbintsc.config.schema.json)。配置读不了
或格式错误时，构建会以 `invalid JSON` 或 `Unable to read` 失败，而不会静默退回默认值。

## 环境变量

| 变量 | 作用 |
| --- | --- |
| `xbintsc_CLANG` | 指定 clang，替代从 `PATH` 解析 |
| `xbintsc_LINKER_ARGS` | 额外链接参数（如 `-fuse-ld=lld`） |
| `xbintsc_CACHE_DIR` | 对象缓存目录（默认 `.xbintsc`） |
| `xbintsc_PREFER_PREBUILT` | `0` 强制从源码编译运行时 |
| `xbintsc_BINARY` | `bin/` 启动器应运行的原生编译器二进制 |

## 增量编译

每次构建的键由入口源码哈希、编译器版本、构建选项、平台和已启用扩展集合共同决定
（[../../src/driver/cache.ts](../../../src/driver/cache.ts)）。如果记录中的产物都还在，
构建立即返回并打印 `(cached)`。C 运行时与扩展源码按同样机制缓存，对象文件放在
`.xbintsc/`（或 `xbintsc_CACHE_DIR`）。

`--force`（或配置 `force: true`）跳过新鲜度检查。如果构建结果疑似过期，这个键是
第一个该看的东西——注意对象缓存的键刻意不含编译参数，这正是运行时覆盖率脚本使用
独立缓存目录的原因。

## 退出码

- `0` —— 成功（对 `run` 而言是被运行程序退出 0）。
- `1` —— 有诊断、用法错误，或编译器自身失败。
- `run` 其他情况直接透传被运行程序的退出码。

## 诊断信息

错误渲染为 `file:line:col - error TS<code>: <message>`，并附带源码片段与插入符下划线。
代码按阶段分段（[../../src/diagnostics/diagnostic.ts](../../../src/diagnostics/diagnostic.ts)）：

| 区间 | 阶段 |
| --- | --- |
| TS1xxx | 词法分析 |
| TS2xxx | 语法分析 |
| TS3xxx | 名称绑定 |
| TS4xxx | 检查器（`TS4005` 即 `UnsupportedFeature`） |
| TS5xxx | 代码生成 |
| TS6xxx | 驱动：`TS6001` 模块未找到、`TS6002` IO、`TS6003` 工具链、`TS6004` 缓存 |

CLI 会在错误之后追加一行 `hint:`，指向对应文档（例如 `TS4005` 指向
[language-support.md](./language-support.md)，`TS6001`/`TS6003` 指向
[troubleshooting.md](./troubleshooting.md)）。工具链与 IO 失败是抛异常而非产生诊断，
CLI 会以 `xbintsc: <message>` 加上同款提示来报告。

## 编程 API

```ts
import { build, compileString } from "xbintsc";
```

- `compileString(source, fileName?, extensions?)` → `{ ir, diagnostics }`。纯 IR 生成：
  不碰文件系统，不调用 clang。
- `build(entryPath, options?)` → `BuildResult`。`options` 支持 `output`、`outDir`、
  `emit`、`optimize`、`force`、`verbose`、`extensions`（注册表）、`app`、`clang`、
  `preferPrebuilt`。
- 返回值形状：`{ outputPath, irPath?, cached, diagnostics, ir?, bundlePath? }`。

`build` 把源码级问题通过 `diagnostics` 报告——使用产物之前务必检查
`diagnostics.some((d) => d.category === "error")`——但 clang 自身失败时会**抛出**
`ToolchainError`（[../../src/driver/toolchain.ts](../../../src/driver/toolchain.ts)）。
子路径 `xbintsc/driver` 另外导出缓存、工具链解析、配置加载、图标与 macOS bundle 助手。
