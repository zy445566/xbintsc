# Runtime deviations

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/deviations.md)

These features **compile and run**, but the result does not fully match ECMAScript:

| Item | Deviation |
| --- | --- |
| RegExp byte-oriented matching | The engine matches UTF-8 bytes: literal multi-byte text works, but `.` and character classes consume one byte per repetition (`\d` / `\w` stay ASCII). Offsets returned by `exec` / `search` / replacement callbacks are converted to UTF-16 code units |
| Global RegExp `lastIndex` | `test` / `exec` do not advance or honour a caller-set `lastIndex` for `/g` / `/y` regexes |
| String `normalize` | Not implemented |
| First-class built-in methods | Implemented as unbound method values (see [Functions](./functions.md)); `fn.toString()` returns a placeholder rather than source text |
| `for...in` | Enumerates own keys of objects / arrays / strings; does not include prototype-chain properties |
| Array out-of-bounds / sparse | Out-of-bounds access returns `undefined`; assigning `arr.length` truncates / extends, but sparse holes are not tracked distinctly |
| Memory management | Non-moving mark-sweep collector behind `xt_alloc`: explicit roots, subsystem root providers and a conservative C-stack scan. Single-threaded and stop-the-world; no moving/compaction or generational mode |
| Function `arity` / argument count | No argument count validation; `fn.length` reports the declared arity but calls are never checked against it |
| `async` / `await` | Real suspension on a stackful coroutine; `await` always queues a microtask (non-promises and already-settled promises included), so interleaving with pending `.then` callbacks matches the specification. Top-level `await` blocks on the event loop rather than yielding the module body. `fs/promises` still performs the I/O synchronously before settling; `fetch` is blocking. Async generators suspend at both `await` and `yield`; `for await...of` awaits one step per iteration |
| Async generator cleanup | `for await...of` does not call the iterator's `return()` when the loop exits early. `yield*` inside an async generator delegates to synchronous iterables only. `.return()` at a suspended `yield` completes without running `finally` (the same limitation as synchronous generators) |
| `super` | `super.x` / `super(...)` takes the prototype of `this`'s prototype; single-level inheritance is correct, but depth > 1 may be inaccurate |
| `Error.stack` | Function-level frames only: each frame is `at name (file:line)` with the function's declaration line (updated to the construction line for the innermost frame); no column numbers and no frames for generator / `async` bodies (they switch stacks) |
| Module live bindings | Namespace imports and imported bindings are snapshots (see [Module system](./modules.md)) |
| `import.meta` | ✓ `import.meta.url` / `.dirname` / `.filename`, resolved from the executable path |

> Number formatting, loose equality, `+` / relational `ToPrimitive`, whole-chain
> optional chaining, `finally` on early exit and object-key ordering have all
> been brought in line with Node and are covered by the differential tests, so
> they are no longer listed as deviations.
