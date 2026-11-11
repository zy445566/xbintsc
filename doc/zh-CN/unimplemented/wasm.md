# WebAssembly

> 属于[未实现语法与特性](../unimplemented.md) · [English](../../unimplemented/wasm.md)

## 尚未实现

- **WebAssembly 字节码执行。** `WebAssembly.compile`、`instantiate`、
  `validate` 以及 `Module` / `Instance` / `Memory` / `Table` / `Global`
  构造器会抛出运行时错误（`WebAssembly is not supported in this build`），
  而不是编译或运行模块。命名空间表面已被识别，因此引用它的代码可以编译
  —— 见[已实现：WebAssembly](../implemented/wasm.md)。
- `WebAssembly.instantiateStreaming` 与流式编译。
- 超出 module/instance/memory/table/global 表面的 WebAssembly 特性：
  bulk memory、SIMD、线程、尾调用、GC、异常处理。
