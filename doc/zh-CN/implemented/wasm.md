# WebAssembly

> 属于[已实现语法与特性](../implemented.md) · [English](../../implemented/wasm.md)

位置：`src/codegen/generator/tables.ts`（命名空间接线）、
`runtime/xt_webassembly.c`（运行时对象）。

## `WebAssembly` 命名空间

- `WebAssembly` 是一等命名空间对象，与 `Math`、`crypto` 类似，
  因此 `const W = WebAssembly` 与 `typeof WebAssembly` 都能工作。
- `WebAssembly.compile`、`WebAssembly.instantiate`、`WebAssembly.validate`
  被识别为静态方法，并降级到命名空间分派器。
- `WebAssembly.Module`、`WebAssembly.Instance`、`WebAssembly.Memory`、
  `WebAssembly.Table`、`WebAssembly.Global`、`WebAssembly.CompileError`、
  `WebAssembly.LinkError`、`WebAssembly.RuntimeError` 被识别为构造器属性。

命名空间及其全部成员都能解析、绑定、检查并降级为二进制。真正编译或执行
WebAssembly 字节码**尚未实现**：这些成员在运行时会抛出错误 —— 见
[未实现：WebAssembly](../unimplemented/wasm.md)。
