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

- Allocator: non-moving mark-sweep collector behind `xt_alloc` (explicit roots, subsystem root providers and a conservative C-stack scan). Collection is deferred while a bundle's module bodies run (`xt_gc_begin_startup` raises the threshold to 512 MiB until `xt_gc_end_startup`): a large module graph is built entirely inside one generated function, and collecting in the middle of it has corrupted live objects. `XT_GC_THRESHOLD` still overrides both values. Two environment switches exist for this class of bug: `XT_GC_VERIFY=1` re-scans
every marked object's payload before the sweep and prints any block it
references that stayed unmarked (a tracer or root gap), and the crash report
names a fault address that falls inside a block the collector freed. I/O worker threads allocate result values but never collect (`xt_gc_worker_thread`): the collector scans the main stack region and is not thread-safe, and a block born during a collection is marked so the sweep keeps it.
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

