# xbintsc Implemented Syntax and Features

This documentation was compiled by checking the source (`src/`, `runtime/`) and
tests (`tests/`) file by file, and lists only the syntax and features that are
**actually usable today**. Implementation locations are noted for traceability.

The pages are split by compiler stage and feature area so a change lands in one
small file instead of a single monolithic document.

> Note: the compiler *parses* much more than it *generates code* for. Many
> TypeScript constructs can be parsed, and even bound, but the code generation
> stage reports `UnsupportedFeature`. Those are **not** listed here; see the
> [unimplemented documentation](./unimplemented.md).

> Language: **English** | [简体中文](./zh-CN/implemented.md)

## Contents

### Pipeline and front end

- [Compilation pipeline](./implemented/pipeline.md) — source → tokens → AST → IR → binary, self-hosting
- [Lexer](./implemented/lexer.md) — tokens, literals, keywords, lexical diagnostics
- [Parser](./implemented/parser.md) — statements, expressions, type syntax, ASI
- [Name binding and scopes](./implemented/binder.md) — scopes, symbols, closure capture

### Semantics and code generation

- [Operators and assignment](./implemented/operators.md) — arithmetic, comparison, logical, bitwise, assignment
- [Value model and calling convention](./implemented/value-model.md) — NaN-boxing, function ABI, closures, prototypes
- [LLVM IR code generation](./implemented/codegen.md) — control flow, expressions, classes, inline caches, entry point

### Runtime and standard library

- [C runtime core](./implemented/runtime-core.md) — allocator/GC, values, conversions, objects, exceptions
- [Strings and text](./implemented/strings.md) — UTF-8/UTF-16 model, string methods, text codecs
- [Standard library](./implemented/stdlib.md) — Object, Array, Number/BigInt/Math, JSON, Date, RegExp, Symbol, errors, console
- [Collections](./implemented/collections.md) — Map, Set, WeakMap, WeakSet, WeakRef, iteration
- [Typed arrays and binary data](./implemented/typed-arrays.md) — typed arrays, ArrayBuffer, DataView
- [Async, promises and generators](./implemented/async.md) — async/await, async generators, promises, timers
- [fetch](./implemented/fetch.md) — reactor-driven HTTP/1.1 client and Headers

### Modules and tooling

- [Module system](./implemented/modules.md) — import/export, bundling, `node_modules`, `import.meta`
- [Extension mechanism](./implemented/extensions.md) — registry, the `node` extension, native C++/Rust extensions
- [Node extension](./implemented/node.md) — 21 Node built-in modules, one page per module
- [Driver, incremental compilation and toolchain](./implemented/driver.md) — cache, clang/LTO/PGO, config, icons
- [CLI and programmatic API](./implemented/cli.md) — commands, flags, embedder API
- [Tests](./implemented/tests.md) — unit, e2e and the differential harness

## Where does a change belong?

Pick the page that owns the feature; that keeps unrelated branches out of the
same file.

| You changed... | Edit |
| --- | --- |
| tokenizer, literals, keywords | [Lexer](./implemented/lexer.md) |
| statement/expression/type syntax | [Parser](./implemented/parser.md) |
| scopes, symbols, captures | [Name binding](./implemented/binder.md) |
| operators, assignment lowering | [Operators](./implemented/operators.md) |
| value representation, function ABI | [Value model](./implemented/value-model.md) |
| control flow, expression lowering, classes, `main` | [Codegen](./implemented/codegen.md) |
| allocator, GC, conversion, exceptions | [Runtime core](./implemented/runtime-core.md) |
| string model and methods | [Strings](./implemented/strings.md) |
| Object/Array/Number/Math/JSON/Date/RegExp/Symbol/errors/console | [Standard library](./implemented/stdlib.md) |
| Map/Set/WeakMap/WeakSet/WeakRef/iteration | [Collections](./implemented/collections.md) |
| typed arrays, ArrayBuffer, DataView | [Typed arrays](./implemented/typed-arrays.md) |
| async/await, promises, generators, timers | [Async](./implemented/async.md) |
| HTTP client | [fetch](./implemented/fetch.md) |
| import/export and bundling | [Modules](./implemented/modules.md) |
| extension registry or the `node` extension | [Extensions](./implemented/extensions.md) |
| a specific Node module (`fs`, `path`, `http`, ...) | [Node extension](./implemented/node.md) |
| build cache, clang, LTO/PGO, config, icons | [Driver](./implemented/driver.md) |
| CLI flags or the embedder API | [CLI](./implemented/cli.md) |
| tests and the differential harness | [Tests](./implemented/tests.md) |
