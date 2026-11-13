# WebAssembly

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/wasm.md)

## Not implemented

- **Non-function imports.** A module that imports a memory, table or global is
  rejected with a `LinkError`; only imported functions are bridged from the JS
  import object.
- **Exported tables and globals.** `WebAssembly.Module.exports` lists them, but
  `instance.exports` only wraps functions and memories. `WebAssembly.Table` and
  `WebAssembly.Global` constructors throw "not supported".
- **Streaming.** `WebAssembly.compileStreaming` and
  `WebAssembly.instantiateStreaming` are not exposed.
- **Custom sections.** `WebAssembly.Module.customSections` returns an empty
  array; section contents are not preserved.
- **Instruction-set coverage beyond wasm3.** The pinned interpreter implements a
  large core subset (the parser also accepts several proposals), but SIMD
  opcodes, the GC proposal, host threads/shared memory and WASI are not
  available. A body using an unsupported opcode fails when the module is
  compiled or instantiated.
- **Multi-module linking.** Importing one WebAssembly module's exports into
  another is not wired up; each instantiation is self-contained plus JS
  function imports.

The execution path itself is implemented (wasm3 behind the namespace) — see
[Implemented: WebAssembly](../implemented/wasm.md).
