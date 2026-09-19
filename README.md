# xtsc

`xtsc` compiles a practical subset of **TypeScript directly to native binaries**.
It parses TypeScript itself, binds names, lowers the program to **LLVM IR text**,
and hands the IR to **clang**, which produces a standalone executable linked
against a small C runtime.

The design goals are:

1. **Incremental compilation** — builds are content-addressed and skipped when nothing changed.
2. **Well-factored modules** — lexer, parser, binder, codegen, driver, extensions each live in their own directory with their own tests.
3. **Extensive tests** — per-module unit tests plus end-to-end tests that compile and run real binaries.
4. **Pluggable extensions** — Node `fs`, Bun APIs, … are optional compile modules, not core code.
5. **Self-hosting ready** — the compiler is written in TypeScript and emits IR text, so a TypeScript rewrite can eventually compile itself.
6. **Multi-platform** — macOS, Linux and Windows on multiple CPUs, validated by GitHub Actions.

## How it works

```
source.ts
   │  lexer      src/lexer
   ▼
 tokens
   │  parser     src/parser        → AST (src/ast)
   ▼
 AST
   │  binder     src/binder        → scopes, symbols, closures
   ▼
 bound AST
   │  codegen    src/codegen       → LLVM IR text (src/codegen/llvm.ts)
   ▼
 module.ll ──clang──► module.o ──link──► executable
                                  ▲
                          runtime/ (C runtime, NaN-boxed values)
```

### Value model

JavaScript values are a single 64-bit word (`xt_value`). Doubles are stored
unboxed; everything else is a tagged pointer: a 16-bit tag in the high bits
plus a 48-bit payload. The representation is defined once in
`src/codegen/values.ts` and `runtime/rt.h`.

### Calling convention

Every compiled function uses the same ABI:

```c
xt_value fn(xt_value env, int32_t argc, xt_value *argv);
```

`env` threads captured variables (by reference, through boxes), so direct calls
and closure calls share one code path. JavaScript semantics that are awkward to
inline (`+` coercion, relational comparison, property access, inspection) are
delegated to `@xt_*` runtime calls.

### Runtime

`runtime/xt_runtime.c` implements strings, objects, arrays, closures, arithmetic,
comparison, exceptions and Node-like `console.log` inspection. It uses a bump
arena and never frees — garbage collection is deliberately deferred and isolated
behind `xt_alloc`, so it can be replaced without touching the compiler.

## Usage

```bash
# Install dependencies
npm install

# Compile and run a program
npx tsx src/cli/main.ts run examples/hello.ts

# Produce a native binary
npx tsx src/cli/main.ts build examples/hello.ts --out build/examples
./build/examples/hello

# Inspect the generated LLVM IR
npx tsx src/cli/main.ts emit examples/hello.ts | head

# Use an optional extension (here, Node's readFileSync)
npx tsx src/cli/main.ts run examples/read-file.ts --ext node
```

CLI options:

```
-o, --output <path>   Output path
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0..-O3              Optimization level (default: -O2)
    --ext <names>     Comma separated extensions (e.g. node)
    --force           Ignore the incremental cache
    --verbose         Print progress information
```

### Programmatic API

```ts
import { build, compileString } from "xtsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

## Incremental compilation

The driver keys each build on the entry source hash, compiler version, options,
platform and the active extension set (`src/driver/cache.ts`). If every recorded
output still exists, the build returns immediately. The C runtime and extension
sources are compiled once and cached on the same principle.

## Extensions

An extension is a plain object (`src/extensions/registry.ts`):

```ts
export const nodeExtension: Extension = {
  name: "node",
  runtimeSources: () => ["runtime/ext_node.c"],
  builtins: () => ({ readFileSync: { symbol: "xt_node_read_text_file" } }),
};
```

Registering it links the extra C source and makes `readFileSync(...)` resolve to
the C symbol with the uniform `(argc, argv)` calling convention. Builtins can be
added without changing the core compiler.

## Tests

```bash
npm run typecheck   # tsc --noEmit
npm test            # unit + end-to-end (runs real binaries when clang is present)
npm run test:e2e    # only the compile-and-run tests
```

Tests are organised by module under `tests/` (`lexer`, `parser`, `binder`,
`codegen`, `driver`, `extensions`, `cli`, `e2e`).

## Requirements

- Node.js 20+
- A `clang`-compatible C compiler on `PATH` (override with `XTSC_CLANG`)

## Language subset

Implemented today: functions, arrow functions and closures, `let`/`const`/`var`,
all common operators, `if`/`while`/`do`/`for`/`for…of`/`for…in`, `break`/
`continue`/`return`/`throw`, objects, arrays, member/element access, assignments,
template literals, `console.log`, extension builtins, and the TypeScript type
syntax is parsed and ignored (types are erased).

Not yet implemented: classes, enums, `switch`, `try`/`catch`, `new`, `this`,
object spread, `async`/generators, and most standard library methods (`Array`
methods other than `push`, `Math`, etc.).
