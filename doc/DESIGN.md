# xbintsc Design

Build a binary compiler for TypeScript.

> Language: **English** | [简体中文](./zh-CN/DESIGN.md)

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
inline (`+` coercion, property access, inspection) are delegated to `@xt_*`
runtime calls. Numeric arithmetic and comparison are the exception: codegen emits
the double fast path inline (`numeric.ts`) and calls the runtime for the coercing
cases.

### Runtime

`runtime/` implements strings, objects, arrays, closures, arithmetic, comparison,
exceptions and Node-like `console.log` inspection. It is split by concern into
translation units (`xt_values.c`, `xt_containers.c`, `xt_stdlib.c`,
`xt_builtins.c`, `xt_io.c`, `xt_alloc.c`, …), each `#include`-ing its
implementation fragments (`.inc`) so files stay small without changing the link
surface. It uses a non-moving mark-sweep collector behind `xt_alloc` (explicit
roots, subsystem root providers and a conservative C-stack scan), so the
collector's internals stay isolated from the compiler.

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
extra C runtime code, linker flags, and the modules it makes importable, mapping
exported bindings to runtime symbols with the uniform `(argc, argv)` ABI. Node's
`import { readFileSync } from "fs"` is wired in through
`src/extensions/node/fs` + `runtime/ext_node/fs`, and the core compiler never
needs to know any platform details.

`nativeObjects` extends the same shape to code built outside the pipeline: a
C++ or Rust project compiled to an `extern "C"` object or static archive can be
linked in and exposed through a JSON manifest (`src/extensions/native.ts`,
`--ext-native`). The driver links the artifacts verbatim and the generator binds
their symbols exactly like the C runtime, so the extension language is invisible
to the core compiler. Windows is covered too: xbintsc links with the toolchain
found on `PATH` (the MSVC ABI when LLVM + the Visual Studio C++ build tools are
installed), and each manifest declares any platform-specific linker flags. See
`examples/extensions/` and `runtime/xt_ext.h` / `runtime/xt_ext.rs`.

## Self-hosting

The compiler compiles itself: `xbintsc build src/cli/main.ts --ext node` produces
a working `xbintsc` binary, and the emitted IR is a fixpoint from the first
generation onward. CI builds those binaries for every supported platform and
checks the fixpoint. The runtime stays in C; rewriting it in TypeScript is an
option for later, not a prerequisite for self-hosting.

## Tests

Organised by module: `tests/lexer`, `tests/parser`, `tests/binder`,
`tests/codegen`, `tests/driver`, `tests/extensions`, `tests/cli`, `tests/e2e`.
When a supported clang (16+, the same version gate `build` applies) is present,
e2e tests really compile and run binaries; otherwise
they are skipped automatically.
