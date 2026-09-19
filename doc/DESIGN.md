# xbintsc Design

Build a binary compiler for TypeScript.

> Language: **English** | [简体中文](./zh-CN/DESIGN.md)

## Original requirements

1. Implement the fundamental JavaScript categories (primitive types, functions, …) so that IR binding is possible later.
2. Parse TypeScript into an AST.
3. Bind IR through LLVM.
4. Compile a TypeScript binary compiler with `llc`.
5. Use that binary compiler to compile the test cases.

Requirements:

1. Incremental compilation.
2. Well-factored modules (parsing and implementation live in separate directories).
3. Extensive tests, organised by module.
4. Optional compile modules as pluggable extensions (e.g. Node's `fs`, Bun's modules).
5. Eventually self-hosting (compile a compiler rewritten in TS with the compiler itself).
6. Multi-platform (Windows, macOS, Ubuntu and multiple CPU architectures), with build artifacts produced by GitHub Actions.

## Architecture

```
source.ts
  │  lexer    src/lexer      lexing: full token set, templates, regex, ASI
  ▼
 tokens
  │  parser   src/parser     recursive descent → AST (src/ast)
  ▼
 AST
  │  binder   src/binder     scopes / symbols / hoisting / closure capture
  ▼
 bound AST
  │  codegen  src/codegen    LLVM IR text (llvm.ts); values.ts defines the value model
  ▼
 module.ll ──clang──► module.o ──link──► executable
                                    ▲
                          runtime/  C runtime (NaN-boxed values)
```

### Value model

`xt_value` is a single 64-bit word. Doubles are stored unboxed; everything else
is a tagged pointer with a 16-bit tag in the high bits plus a 48-bit payload.
The representation is defined once in `src/codegen/values.ts` and `runtime/rt.h`
and shared by the compiler and the runtime.

### Calling convention

Every compiled function uses the same ABI:

```c
xt_value fn(xt_value env, int32_t argc, xt_value *argv);
```

`env` threads captured variables by reference through boxes, so direct calls and
closure calls share a single code path. JavaScript semantics that are awkward to
inline (`+` coercion, relational comparison, property access, inspection) are
delegated to `@xt_*` runtime calls.

### Runtime

`runtime/xt_runtime.c` implements strings, objects, arrays, closures, arithmetic,
comparison, exceptions and Node-like `console.log` inspection. It uses a bump
arena and never frees — garbage collection is deliberately deferred and isolated
behind `xt_alloc`, so it can later be replaced without touching the compiler.

### Replacing llc

LLVM/`llc` is not installed on the host, but `clang` can compile LLVM IR text
directly, so the pipeline is `IR text → clang -c → object file → link C runtime`.
This keeps the "IR binding + native compilation" goal while remaining
cross-platform.

## Directory structure

| Directory | Responsibility |
| --- | --- |
| `src/lexer` | Lexing (Scanner, TokenKind) |
| `src/ast` | AST nodes, factories, visitors |
| `src/parser` | Recursive descent parser (including type syntax) |
| `src/binder` | Scope and symbol resolution, closure capture |
| `src/codegen` | Value model and LLVM IR generation |
| `src/diagnostics` | Source files, diagnostics, hashing |
| `src/driver` | Pipeline driver, incremental cache, clang toolchain wrapper |
| `src/extensions` | Pluggable extension registry and Node extension |
| `src/cli` | Command-line entry point |
| `runtime` | C runtime and `rt.h` |
| `tests` | Tests organised by module (including e2e) |
| `scripts` | Runtime build and other scripts |
| `.github/workflows` | Multi-platform, multi-version CI |

## Incremental compilation

`src/driver/cache.ts` keys the cache on the entry source hash, compiler version,
compile options, platform and extension set; when all recorded outputs still
exist the build is skipped. The runtime C sources are cached as object files by
content hash in the same way.

## Extension mechanism

An extension is a plain object (see `src/extensions/registry.ts`): it declares
extra C runtime code, linker flags, and maps global functions to runtime symbols
with the uniform `(argc, argv)` ABI. Node's `readFileSync` is wired in through
`src/extensions/node/fs` + `runtime/ext_node/fs`, and the core compiler never
needs to know any platform details.

## Self-hosting roadmap

The compiler itself is written in TypeScript and emits IR text, and `runtime` is
already decoupled from the compiler. Later, the runtime can be rewritten in TS
and the compiler can compile itself, gradually reaching self-hosting.

## Tests

Organised by module: `tests/lexer`, `tests/parser`, `tests/binder`,
`tests/codegen`, `tests/driver`, `tests/extensions`, `tests/cli`, `tests/e2e`.
When `clang` is present, e2e tests really compile and run binaries; otherwise
they are skipped automatically.
