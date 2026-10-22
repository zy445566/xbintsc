# Toolchain, platform and engineering

> Part of [Unimplemented syntax and features](../unimplemented.md) · [简体中文](../zh-CN/unimplemented/toolchain.md)

| Item | Status |
| --- | --- |
| GC (garbage collection) | ✓ non-moving mark-sweep in `xt_alloc.c`, armed by a live-heap threshold (`XT_GC_THRESHOLD` pins it) or `xt_gc_collect()`; after each collection the threshold becomes twice the surviving heap (floor 1 MiB), so the resident set follows the live set instead of only growing. `XT_GC_TRACE=1` prints one line per collection to stderr. Explicit roots + root providers + conservative active-stack scan. `WeakMap`/`WeakSet` keys are ephemerons (a value is retained only while its key is reachable) and `WeakRef` targets are cleared when unreachable; because the stack scan is conservative, a stale stack word can delay that clearing. No compaction or background collection |
| Self-hosting | ✓ the compiler compiles itself: `xbintsc build src/cli/main.ts` produces a working binary, and the emitted IR is stable from generation 1 onward. The runtime is still C |
| Type checker | ✗ only diagnostic codes are defined; no checker |
| Full standard library | partial: Math / JSON / Date / Map / Set / RegExp / `Error` / `BigInt` / `Symbol` / `structuredClone` implemented; String.normalize missing |
| Multi-file module bundling | partial: relative-path `.ts` bundling, ESM `node_modules` packages, namespace imports and bare-specifier extension module imports implemented; CommonJS is handled only inside `node_modules`; circular dependencies / live bindings are not implemented |
| Real async runtime | ✓ `async`/`await` suspends on pending promises on the `select(2)` reactor: timers, TCP sockets and promise callbacks resume the coroutine. Still missing: truly asynchronous I/O scheduling (`fs/promises` blocks before settling), a non-blocking `fetch` and worker threads (workers are child processes) |
| Windows binary artifact verification | adapted at the build layer (`.exe` suffix, link flag branch), verified in CI |
| Precise ECMAScript number / string / comparison semantics | partial, see [Runtime deviations](./deviations.md) |
