# `assert` 模块

> 属于[Node 扩展：已实现](../node.md) · [English](../../../implemented/node/assert.md)

实现位置：`src/extensions/node/assert/index.ts`、`runtime/ext_node/assert/assert.c`

`assert.ok` / `fail` / `equal` / `notEqual` / `strictEqual` / `notStrictEqual` /
`deepEqual` / `notDeepEqual` / `deepStrictEqual` / `notDeepStrictEqual` /
`throws` / `doesNotThrow` / `rejects` / `doesNotReject` / `match` /
`doesNotMatch` / `ifError`。

具名导入与命名空间形式都分发到 `xt_assert_static`：

```ts
import assert from "node:assert";
assert.strictEqual(1 + 1, 2);
```
