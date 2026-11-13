# WebAssembly

> 属于[未实现语法与特性](../unimplemented.md) · [English](../../unimplemented/wasm.md)

## 尚未实现

- **非函数导入。** 导入 memory、table 或 global 的模块会以 `LinkError` 拒绝；
  目前只从 JS 导入对象桥接函数。
- **导出的 table 与 global。** `WebAssembly.Module.exports` 会列出它们，但
  `instance.exports` 只包装函数和内存。`WebAssembly.Table` 与
  `WebAssembly.Global` 构造器会抛“not supported”。
- **流式接口。** 未暴露 `WebAssembly.compileStreaming` 与
  `WebAssembly.instantiateStreaming`。
- **自定义段。** `WebAssembly.Module.customSections` 返回空数组，不保留段内容。
- **超出 wasm3 的指令集。** 固定版本解释器实现了较大的核心子集（解析器也接受
  若干提案），但不支持 SIMD 指令、GC 提案、宿主线程/共享内存和 WASI。使用
  不支持操作码的函数体会在编译/实例化模块时失败。
- **多模块链接。** 尚未接入把一个 WebAssembly 模块的导出作为另一个模块的
  导入；每次实例化都是自包含的，外加 JS 函数导入。

执行路径本身已经实现（wasm3 位于命名空间之后）—— 见
[已实现：WebAssembly](../implemented/wasm.md)。
