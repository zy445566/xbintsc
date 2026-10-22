# `test`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/test.md)

Location: `src/extensions/node/test/index.ts`, `runtime/ext_node/test/test.c`

- `test(name, fn)` runs `fn` immediately, catches assertion failures and reports TAP output.
- `it` / `describe` / `skip` / `todo` / `only` and the `before` / `after` / `beforeEach` / `afterEach` hooks.
- A failure makes the process exit non-zero.
- The default export is callable, so `import test from "node:test"` works.
