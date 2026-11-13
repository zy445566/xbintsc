# WebAssembly

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/wasm.md)

Location: `src/extensions/wasm.ts` (link feature), `runtime/xt_wasm/` (bridge and
API), `runtime/xt_webassembly.c` (core namespace stub), `vendor/wasm3/`
(interpreter, fetched by `npm run vendor`).

## The `wasm` link feature

`WebAssembly` is a first-class namespace object, like `Math` or `crypto`, so
`const W = WebAssembly` and `typeof WebAssembly` both work. The interpreter
behind it is not linked into every program: a usage scan selects the **`wasm`
link feature** when the bundled source mentions `WebAssembly`, and only then
compiles the pinned wasm3 sources plus the bridge. `--feature wasm` forces it on
(a program that reaches `globalThis["WebAssembly"]` through a string needs
that), and `--no-feature wasm` keeps it out, in which case the namespace members
throw `WebAssembly is not supported in this build` at run time.

The feature is *required*: if `vendor/wasm3` is missing, the build fails with the
`npm run vendor` instruction instead of warning and producing a binary with an
unresolved symbol.

## Supported API

| Surface | Behaviour |
| --- | --- |
| `WebAssembly.validate(bytes)` | parses/validates a module; invalid bytecode returns `false` |
| `WebAssembly.compile(bytes)` | `Promise<Module>`; invalid bytecode rejects with `CompileError` |
| `WebAssembly.instantiate(bytes, imports?)` | `Promise<{ module, instance }>` |
| `WebAssembly.instantiate(module, imports?)` | `Promise<Instance>` |
| `new WebAssembly.Module(bytes)` | synchronous compile; throws `CompileError` |
| `new WebAssembly.Instance(module, imports?)` | synchronous instantiation; throws `LinkError` |
| `WebAssembly.Module.exports(module)` / `.imports(module)` | descriptor arrays (`{ name, kind }` / `{ module, name, kind }`) |
| `WebAssembly.Module.customSections(module, name)` | returns `[]` (custom sections are not exposed yet) |
| `new WebAssembly.Memory({ initial, maximum? })` | a standalone page-backed `ArrayBuffer` with `buffer` and `grow()` |
| `instance.exports.<function>` | callable from JS; `i32`/`f32`/`f64` are numbers, `i64` is a BigInt |
| `instance.exports.<memory>` | `.buffer` is an `ArrayBuffer` snapshot and `.grow(pages)` grows the wasm memory |
| JS function imports | called from wasm with their declared numeric types; a thrown JS exception propagates back to the wasm caller |
| `CompileError` / `LinkError` / `RuntimeError` | real `Error` subclasses; `instanceof WebAssembly.CompileError` and `instanceof Error` work |

Both `WebAssembly.compile` and `instantiate` settle through the normal promise
queue, so `await` works and timers/sockets continue to run while they are
pending.

### Memory is synchronised around calls

The runtime's `ArrayBuffer` is a JavaScript-heap byte array, not a live alias of
wasm3's linear memory. `instance.exports.<memory>.buffer` copies the wasm bytes
out, and every exported function call copies the JS buffer into wasm memory
before the call and back out afterwards. Reads and writes made through a typed
array are therefore visible to the next wasm call (and vice versa), but a view
is not a zero-copy window: a write is observed on the next call or `.buffer`
read, not instantly.

### `.wasm` imports

The core extension installs a binary asset loader, so a module can import a wasm
file directly:

```ts
import bytes from "./add.wasm";

const { instance } = await WebAssembly.instantiate(bytes, {});
console.log(instance.exports.add(2, 3));
```

The bytes are embedded in the executable (base64 in the generated module), so
the built program needs no `.wasm` file at run time.

## Limits

- Only **function** imports are bridged. A module that imports a memory, table or
  global is rejected with a `LinkError`.
- Exported **tables** and **globals** are listed by `Module.exports` but are not
  wrapped on `instance.exports`; only functions and memories are.
- `WebAssembly.Table` and `WebAssembly.Global` constructors are not implemented.
- `compileStreaming` / `instantiateStreaming` are not exposed.
- wasm3's supported instruction set bounds execution (no SIMD opcodes, no GC,
  no host threads); the parser accepts the proposals the pinned wasm3 build
  enables, and an unsupported body fails with a WebAssembly error when the
  module is compiled or instantiated.

See [Unimplemented: WebAssembly](../unimplemented/wasm.md) for the tracked gaps.
