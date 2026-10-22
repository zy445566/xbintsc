# 全局对象与命名空间

> 属于[Node 扩展：未实现](../node.md) · [English](../../../unimplemented/node/globals.md)

| 未实现 | 说明 |
| --- | --- |
| `global` / `globalThis` | 无 |
| `__dirname` / `__filename` | 无 |
| `require` / `module` / `exports` | 仅限 `node_modules` 内的包（打包器会降级）；用户代码需使用 ESM `import` |
| 未 `import` 的 `fs.readFileSync(...)` | ✗ 不支持。`fs` 不是全局对象；请先导入（`import fs from "fs"` 或 `import * as fs from "node:fs"`），之后 `fs.readFileSync(...)` 会下降为该模块的运行时符号。 |

Node 模块通过裸名称或 `node:` 前缀的 `import` 引入（`import { readFileSync } from "fs"`、`import path from "path"`、`import { platform } from "node:os"`）。具名与命名空间导入都会解析到扩展模块的运行时入口。

已支持的命名空间调用：`path.*`、`os.*`、`process.*`、`fs.*`、`fs/promises.*`（方法）、`child_process.*`、`crypto.*`、`url.*`（需先导入；`path`/`os`/`process` 也可使用全局名），以及基于分发器的 `Buffer.*`、`stream.*`、`net.*`、`dgram.*`、`http.*` 和 `process.platform` / `process.argv` 等属性。`Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough` / `Buffer` 亦可用作全局构造函数。
