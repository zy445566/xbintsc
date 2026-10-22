# Global objects and namespaces

> Part of [Node extension: unimplemented](../node.md) · [简体中文](../../zh-CN/unimplemented/node/globals.md)

| Unimplemented | Notes |
| --- | --- |
| `global` / `globalThis` | none |
| `__dirname` / `__filename` | none |
| `require` / `module` / `exports` | only inside `node_modules` packages (the bundler lowers them); user code must use ESM `import` |
| `fs.readFileSync(...)` without an `import` | ✗ unsupported. `fs` is not a global; import it first (`import fs from "fs"` or `import * as fs from "node:fs"`) and `fs.readFileSync(...)` lowers to the module's runtime symbol. |

Node modules are reached through `import` with a bare or `node:`-prefixed
specifier (`import { readFileSync } from "fs"`, `import path from "path"`,
`import { platform } from "node:os"`). Named and namespace imports both resolve
to the extension module's runtime entries.

Supported namespace calls: `path.*`, `os.*`, `process.*`, `fs.*`,
`fs/promises.*` (methods), `child_process.*`, `crypto.*`, `url.*` once imported
(a namespace/default import or, for `path`/`os`/`process`, the global name),
plus the dispatcher-backed namespaces `Buffer.*`, `stream.*`, `net.*`,
`dgram.*`, `http.*` and properties such as `process.platform` /
`process.argv`. `Readable` / `Writable` / `Duplex` / `Transform` / `PassThrough`
/ `Buffer` can also be used as global constructors.
