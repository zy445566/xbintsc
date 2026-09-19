# xbintsc

`xbintsc` compiles a practical subset of **TypeScript directly to native binaries**.
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

There are two ways to invoke the CLI:

- **As a user** — the package is installed and the `xbintsc` command is available
  (via the `bin` entry `./bin/xbintsc.js`).
- **As a developer** — running directly from a source checkout, before building.

### As a user

```bash
# Install globally
npm install -g xbintsc

# ...or use it on demand without installing
npx xbintsc version

# Compile and run a program
xbintsc run examples/hello.ts

# Produce a native binary
xbintsc build examples/hello.ts --out build/examples
./build/examples/hello

# Inspect the generated LLVM IR
xbintsc emit examples/hello.ts | head

# Use an optional extension (here, Node's readFileSync)
xbintsc run examples/read-file.ts --ext node
```

### As a developer (from source)

In a source checkout, run the TypeScript sources directly through `tsx` (or use
`npm run xbintsc`). The `bin` launcher also works here: without a `dist/` build it
falls back to `tsx` automatically.

```bash
# Install dependencies
npm install

# Compile and run a program
npm run xbintsc -- run examples/hello.ts
# equivalently
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
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

## Incremental compilation

The driver keys each build on the entry source hash, compiler version, options,
platform and the active extension set (`src/driver/cache.ts`). If every recorded
output still exists, the build returns immediately. The C runtime and extension
sources are compiled once and cached on the same principle.

## Extensions

An extension is a plain object (`src/extensions/registry.ts`). The `node`
extension is itself split into one folder per Node module, each pairing its
builtins with the C sources that implement them:

```
src/extensions/node/       runtime/ext_node/
  index.ts   # nodeExtension  fs/read_file.c
  fs/index.ts               
  fs/read-file.ts           
```

```ts
// src/extensions/node/index.ts
const modules: readonly NodeModule[] = [fsModule];

export const nodeExtension: Extension = {
  name: "node",
  runtimeSources: () => modules.flatMap((m) => m.runtimeSources()),
  builtins: () => modules.reduce(
    (merged, m) => Object.assign(merged, m.builtins()),
    {} as Record<string, BuiltinFunction>,
  ),
};
```

Registering it links the extra C sources and makes `readFileSync(...)` resolve
to the C symbol with the uniform `(argc, argv)` calling convention. Adding a
module means dropping a folder under `src/extensions/node/` and its C
counterpart under `runtime/ext_node/`; the core compiler never changes.

Node module coverage:

- [Node extension: implemented](./doc/node-implemented.md)
- [Node extension: unimplemented](./doc/node-unimplemented.md)

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
- A `clang`-compatible C compiler on `PATH` (override with `xbintsc_CLANG`)

## Language subset

- [Implemented features](./doc/implemented.md)
- [Unimplemented features](./doc/unimplemented.md)
