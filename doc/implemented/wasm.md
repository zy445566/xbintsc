# WebAssembly

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/wasm.md)

Location: `src/codegen/generator/tables.ts` (namespace wiring),
`runtime/xt_webassembly.c` (runtime object).

## The `WebAssembly` namespace

- `WebAssembly` is a first-class namespace object, like `Math` or `crypto`,
  so `const W = WebAssembly` and `typeof WebAssembly` both work.
- `WebAssembly.compile`, `WebAssembly.instantiate` and `WebAssembly.validate`
  are recognized as static methods and lower to the namespace dispatcher.
- `WebAssembly.Module`, `WebAssembly.Instance`, `WebAssembly.Memory`,
  `WebAssembly.Table`, `WebAssembly.Global`, `WebAssembly.CompileError`,
  `WebAssembly.LinkError` and `WebAssembly.RuntimeError` are recognized as
  constructor properties.

The namespace and every one of those members parse, bind, check and lower to a
binary. Actually compiling or running WebAssembly bytecode is **not**
implemented: the members throw at run time — see
[Unimplemented: WebAssembly](../unimplemented/wasm.md).
