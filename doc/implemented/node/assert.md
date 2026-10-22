# `assert`

> Part of [Node extension: implemented](../node.md) · [简体中文](../../zh-CN/implemented/node/assert.md)

Location: `src/extensions/node/assert/index.ts`, `runtime/ext_node/assert/assert.c`

`assert.ok` / `fail` / `equal` / `notEqual` / `strictEqual` / `notStrictEqual` /
`deepEqual` / `notDeepEqual` / `deepStrictEqual` / `notDeepStrictEqual` /
`throws` / `doesNotThrow` / `rejects` / `doesNotReject` / `match` /
`doesNotMatch` / `ifError`.

Named imports and the namespace form both dispatch through `xt_assert_static`:

```ts
import assert from "node:assert";
assert.strictEqual(1 + 1, 2);
```
