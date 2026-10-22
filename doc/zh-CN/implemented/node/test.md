# `test` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/test.md)

实现位置：`src/extensions/node/test/index.ts`、`runtime/ext_node/test/test.c`

- `test(name, fn)` 立即执行 `fn`，捕获断言失败并输出 TAP。
- `it` / `describe` / `skip` / `todo` / `only` 以及 `before` / `after` / `beforeEach` / `afterEach` 钩子。
- 失败时进程以非零码退出。
- 默认导出可调用，因此 `import test from "node:test"` 可用。
