# 构建配方

常见任务的可直接复制命令，以及让构建成功的关键前提。完整参数见
[cli.md](./cli.md)；命令报错见 [troubleshooting.md](./troubleshooting.md)。

## 调用编译器

| 场景 | 调用方式 |
| --- | --- |
| 已发布的独立压缩包 | `xbintsc …`（`bin/xbintsc[.exe]` 在 `PATH` 上） |
| 源码检出，已编译 | `node dist/src/cli/main.js …` |
| 源码检出，直接跑 TypeScript | `npx tsx src/cli/main.ts …` |
| 源码检出，npm 脚本 | `npm run xbintsc -- …` |
| 源码检出，bin 启动器 | `node bin/xbintsc.js …`（会自动回退到 `tsx`） |

下文为简洁统一写作 `xbintsc`。

## 运行程序

```bash
xbintsc run app.ts
xbintsc run app.ts -- --flag value        # -- 之后的所有参数传给被运行的程序
```

`run` 先把程序编译成可执行文件，再以继承 stdio 的方式启动它。它要求
`--emit exe`；`xbintsc run app.ts --emit ir` 是错误用法。`run` 的退出码就是被运行
程序的退出码。

## 构建独立二进制

```bash
xbintsc build app.ts                      # -> build/app（Windows 上是 build/app.exe）
xbintsc build app.ts --out build/app      # 指定输出目录
xbintsc build app.ts -o app.bin            # 或指定确切输出路径
xbintsc build app.ts -O0                   # 优化级别：-O0 .. -O3（默认 -O2）
xbintsc build app.ts --force               # 忽略增量缓存
xbintsc build app.ts --verbose             # 输出进度
```

`build` 会打印产出的文件；如果增量缓存让本次构建无需干活，会额外打印
`(cached)`：

```
xbintsc: wrote /abs/path/build/app
```

其他产出类型用于检查：

```bash
xbintsc build app.ts --emit ir   # 写出 app.ll
xbintsc build app.ts --emit obj  # 写出 app.o
```

## 查看 LLVM IR

```bash
xbintsc emit app.ts > app.ll     # IR 输出到 stdout，不写任何文件
```

`emit` 不需要 clang，也不需要运行时库——它是确认"编译器到底理解了什么"最便宜
的手段，也是不确定某个语法是否被支持时最快的反馈回路。

## 能编译的程序形态

```ts
// app.ts —— ESM，不用 require()
import { readFileSync } from "fs";          // 需要 `--ext node`

function main(): void {
  const text = readFileSync("package.json", "utf8");
  console.log(text.length);
}

main();                                     // 顶层代码按顺序执行
```

- 使用 `import`/`export`；`require()` 会被拒绝。
- 相对导入会被打包（`import { helper } from "./helper.js"` 会解析到
  `helper.ts`）。
- Node 内置模块用裸名（`fs`、`path`、`node:fs`），并且需要开启 `node` 扩展。

## 项目配置：不带参数即可构建

`xbintsc.config.json`（从入口文件所在目录向上查找，或用 `--config <path>` 指定，
用 `--no-config` 关闭）承载构建选项，因此只敲 `xbintsc build` 就能工作。配置里的
路径相对该配置文件解析，任何命令行参数都会覆盖对应字段。

```json
{
  "entry": "src/app.ts",
  "outDir": "build",
  "optimize": "2",
  "extensions": ["node"],
  "app": { "name": "Demo", "icon": "assets/app.png" }
}
```

机器可读的 schema 见 [../xbintsc.config.schema.json](../../xbintsc.config.schema.json)；
完整字段表见 [cli.md](./cli.md)。

## 带扩展编译

只要 import 了 Node 模块，就必须开启对应扩展，否则编译失败。可一次开多个，用逗号
分隔：

```bash
xbintsc run app.ts --ext node
xbintsc run app.ts --ext node,gui
```

C++/Rust 库改用清单注册：

```bash
xbintsc run app.ts --ext-native ./mathx.manifest.json
```

两种扩展的编写方式都在 [extensions.md](./extensions.md)。

## 编程 API

写工具而不是写程序时：

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");   // IR 文本，不需要 clang
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

`build` 返回 `{ outputPath, irPath?, cached, diagnostics, bundlePath? }`，可恢复的
问题通过 `diagnostics` 报告；但工具链失败会**抛异常**。子路径导出
`xbintsc/driver` 暴露驱动内部能力。
