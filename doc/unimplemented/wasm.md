# WebAssembly

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/wasm.md)

## Not implemented

- **WebAssembly bytecode execution.** `WebAssembly.compile`, `instantiate`,
  `validate` and the `Module` / `Instance` / `Memory` / `Table` / `Global`
  constructors throw a runtime error (`WebAssembly is not supported in this
  build`) instead of compiling or running a module. The namespace surface is
  recognized so code that references it compiles — see
  [Implemented: WebAssembly](../implemented/wasm.md).
- `WebAssembly.instantiateStreaming` and streaming compilation.
- WebAssembly features beyond the module/instance/memory/table/global surface:
  bulk memory, SIMD, threads, tail calls, GC, exception handling.
