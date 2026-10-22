# `path` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/path.md)

实现位置：`src/extensions/node/path/index.ts`、`runtime/ext_node/path/path.c`

采用 `path.<name>(...)` 命名空间调用，编译器将其降为 `xt_path_static(<name>, argc, argv)`。结果在所有平台上都以 POSIX `/` 分隔符输出（Windows 也接受 `/`），但输入可以使用 Windows 原生分隔符：在 Windows 上 `/` 与 `\` 都被识别，并保留盘符前缀（`C:`），因此自举后的编译器能正确解析带盘符的路径。以命名空间方式导入（`import path from "path"` / `import * as path from "path"`），或单独导入方法（`import { join } from "path"`）。

| 方法 | 说明 |
| --- | --- |
| `path.join(...parts)` | 拼接并规范化 |
| `path.resolve(...parts)` | 解析为绝对路径 |
| `path.normalize(path)` | 规范化 |
| `path.dirname(path)` | 目录名 |
| `path.basename(path[, ext])` | 文件名，可去掉扩展名 |
| `path.extname(path)` | 扩展名 |
| `path.isAbsolute(path)` | 是否绝对路径 |
| `path.relative(from, to)` | 相对路径 |

```ts
import path from "path";
import { basename } from "path";

console.log(path.join("a", "b", "..", "c")); // a/c
console.log(basename("/x/y/z.txt"));          // z.txt
```
