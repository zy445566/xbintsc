# C runtime core

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/runtime-core.md)

Location: `runtime/xt_alloc.c`, `runtime/xt_values.c`, `runtime/xt_containers.c`,
`runtime/xt_builtins.c`, `runtime/xt_constructors.c`, `runtime/xt_io.c`, sharing the
private header `runtime/rt_internal.h`; the public ABI is in `runtime/rt.h`.

Strings, symbols, typed arrays, collections, promises/async, `fetch` and the
standard-library surface have their own pages:

- [Strings and text](./strings.md)
- [Standard library](./stdlib.md)
- [Collections](./collections.md)
- [Typed arrays and binary data](./typed-arrays.md)
- [Async, promises and generators](./async.md)
- [fetch](./fetch.md)

- Allocator: non-moving mark-sweep collector behind `xt_alloc` (explicit roots, subsystem root providers and a conservative C-stack scan). Collection is deferred while a bundle's module bodies run (`xt_gc_begin_startup` raises the threshold to 512 MiB until `xt_gc_end_startup`): a large module graph is built entirely inside one generated function, and collecting in the middle of it has corrupted live objects. `XT_GC_THRESHOLD` still overrides both values. The next threshold is **4×** the surviving heap: an interactive scene keeps a large live set for minutes, and at 2× a three.js page collected once every ~1.2 s; 4× halves the number of stop-the-world pauses while the threshold still follows (and shrinks with) the live set, so it cannot ratchet upwards. `XT_GC_TRACE=1` prints each collection's heap, allocation and live-kind counts plus its `took <ms>` broken into table/mark/sweep phases. Two environment switches exist for this class of bug: `XT_GC_VERIFY=1` re-scans
every marked object's payload before the sweep and prints any block it
references that stayed unmarked (a tracer or root gap), and the crash report
names a fault address that falls inside a block the collector freed. I/O worker threads allocate result values but never collect (`xt_gc_worker_thread`): the collector scans the main stack region and is not thread-safe, and a block born during a collection is marked so the sweep keeps it.
- Small-block allocation: blocks up to 2 KiB (most objects, closures, strings, and the property/array buffers behind them) come from per-size-class free lists carved out of 1 MiB slabs instead of `malloc`/`calloc`; the collector returns a block to its class, so a steady state reuses memory and object-heavy scene code stops paying a C allocator round trip per object. Large blocks keep `malloc`/`free`, I/O workers keep `calloc` (the pool is main-thread only), and a pooled block is marked by `XT_GC_POOLED` so the sweep releases it correctly.
- Tracing is **precise** for the kinds a program allocates in bulk: an `xt_object` marks its prototype, error stack and each property's key/value/getter/setter, an array its items and `extra`, a function its name bag, prototype and environment, and maps/sets/regexps their element arrays. Only the exotic kinds (iterators, symbols) still use the conservative payload scan. `xt_gc_mark_value` rejects every number with one tag compare, so marking a 400k-object scene does not spend its time in five `XT_IS_*` branches per property slot.
- The C-string property lookups behind the host bindings (`xt_object_get_cstr(w, "__xt_gl_context")`, the typed-array helpers, the runtime's own helpers) intern the key contents in a small, GC-rooted table instead of allocating an `xt_string` per call. This was the dominant allocation source for a WebGL page: the Saltline boot made ~24M such calls, and after the table the same run makes ~165k calls and allocates ~192k strings in total, roughly two orders of magnitude less garbage for the collector to sweep.
- Time: `performance.now()` returns milliseconds since the **program's** time origin, like a browser's navigation start or Node's process start, not the raw monotonic counter (on Windows the QPC counts from machine boot, so a process started six days into an uptime used to report six days on its first call). `xt_time_init()` snapshots the origin from the generated `main`, and animation-frame callbacks receive the same clock.
- Profiling: `XT_PROFILE=1` counts entries to the hottest runtime helpers (allocation, collection, property get/set, method dispatch, closure calls, array element reads, typed-array reads/writes, `Math` dispatch) and prints them to stderr at exit. It also breaks allocations down by object kind and lists the twenty most frequent C-string property names, which attributes allocation pressure to a helper or a key without a sampling profiler. The counters cost one branch when enabled and nothing when unset; pair them with a microbenchmark of the same helper to attribute time.
- Value construction: `xt_undefined/xt_null/xt_bool/xt_number/xt_string_new/xt_string_from_cstr`.
- Type conversion: `xt_truthy`, `xt_to_number`, `xt_to_string`, `xt_typeof`.
- Arithmetic: `add/sub/mul/div/mod/pow/neg/pos`.
- Bitwise: `and/or/xor/not/shl/shr/ushr` (including `ToInt32` semantics).
- Comparison: `lt/le/gt/ge`, loose / strict equality, `not`, `is_nullish`.
- Objects: linear property list, `object_new/get/set/has/keys/values/entries/assign/spread`.
- Arrays: `array_new/get/set/push/length/spread`; assigning `arr.length` truncates or extends (matching JS); `iter_length` / `iter_value` expose a uniform iteration view over arrays, strings (by code point), `Map` and `Set`.
- Generic member access: `xt_get` / `xt_set` (dispatch over arrays / objects / strings).
- Operator helpers: `xt_in` (`in`), `xt_delete` (`delete`), `xt_rest_args` (rest parameters / `arguments`).
- Box: `box_new/get/set` (for closure-captured variables).
- Functions and closures: `arg`, `closure_new/call/env/arity`.
- Exceptions: `xt_try_enter` / `xt_try_exception` / `xt_try_leave` maintain the `_setjmp` frame stack; `xt_throw` long-jumps to the nearest `try` when a frame exists, otherwise prints `Uncaught ...` and exits. A promise rejection nobody observes is fatal too: `xt_promise_report_unhandled` runs once the event loop drains, prints `Unhandled promise rejection` plus the reason and exits with status 1 (see [Async](./async.md)). Codegen also maintains a shadow call stack (function name, source file and line) that `Error.stack` captures (see [Standard library](./stdlib.md)), and a fatal-signal handler dumps the same frames on a native crash. While a bundle's module bodies run, the stack names the module being initialised (`    module: <path>` above the frames): every module shares one generated function, so its frames alone all carry the entry file's coordinates. Setting `XT_STACK_WATCHDOG_MS=<n>` starts a sampler thread that prints those frames whenever no function has been entered or left for *n* milliseconds, turning a silent hang (an unexpected infinite loop, a blocking host call) into a source location; the thread is not started when the variable is unset. `--debug` additionally emits `!DICompileUnit` / `!DISubprogram` / `!DILocation` metadata and compiles with `-g -fno-omit-frame-pointer`, so gdb/lldb/WinDbg map instructions back to source lines. `!DILocation` has a 16-bit `column` field, so a column from a minified bundle (they run to hundreds of thousands) is clamped to 65535 instead of making clang reject the module; the line is clamped to the 24-bit limit for the same reason.
- Typed pointers that the collector follows from inside a struct (a promise's reaction list, an active coroutine frame, a string's rope children) are validated against the heap table before marking, so a stale one is skipped instead of dereferenced (`XT_GC_VERIFY=1` reports it).
- Objects / functions: `xt_object` with a prototype chain, `xt_function` with a property bag (static members and `prototype`); `xt_new` (instantiation), `xt_instance_of` (prototype chain), `xt_object_freeze/is_frozen/from_entries`; `xt_constructors.c` holds the first-class constructor cache, constructor prototypes and static-value dispatch.

