# `path`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/path.md)

Location: `src/extensions/node/path/index.ts`, `runtime/ext_node/path/path.c`

Uses `path.<name>(...)` namespace calls, which the compiler lowers to
`xt_path_static(<name>, argc, argv)`. Results are produced with the POSIX `/`
separator on every platform (Windows accepts `/`), while inputs may use native
Windows separators: on Windows both `/` and `\` are recognised and drive
prefixes (`C:`) are preserved, so the self-hosted compiler resolves
drive-letter paths correctly.
Import the module as a namespace (`import path from "path"` / `import * as path
from "path"`) or pull in individual methods (`import { join } from "path"`).

| Method | Notes |
| --- | --- |
| `path.join(...parts)` | join and normalize |
| `path.resolve(...parts)` | resolve to an absolute path |
| `path.normalize(path)` | normalize |
| `path.dirname(path)` | directory name |
| `path.basename(path[, ext])` | file name, optionally without extension |
| `path.extname(path)` | extension |
| `path.isAbsolute(path)` | whether it is an absolute path |
| `path.relative(from, to)` | relative path |

```ts
import path from "path";
import { basename } from "path";

console.log(path.join("a", "b", "..", "c")); // a/c
console.log(basename("/x/y/z.txt"));          // z.txt
```
