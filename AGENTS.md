# AGENTS.md — working in the xbintsc repository

**xbintsc is a compiler: it turns a subset of TypeScript into standalone native
binaries.** It lexes and parses TypeScript itself, binds names, lowers the
program to **LLVM IR text**, and hands that text to **clang**, which produces an
executable linked against a small C runtime. There is no Node, V8 or TypeScript
compiler in the produced binary.

Read this file first. Then read **one** of the documents below — the one that
matches your task. Do not read all of `doc/`.

## Which task are you doing?

| Your task | Read next |
| --- | --- |
| Write/compile a TypeScript program *with* xbintsc | [doc/ai/cli.md](./doc/ai/cli.md) |
| Check whether a syntax or API is supported | [doc/ai/language-support.md](./doc/ai/language-support.md) |
| Use `fs`, `http`, `crypto`, … or a C++/Rust library | [doc/ai/extensions.md](./doc/ai/extensions.md) |
| A build failed and you need to fix it | [doc/ai/troubleshooting.md](./doc/ai/troubleshooting.md) |
| Change the compiler, runtime or tests | [doc/ai/contributing.md](./doc/ai/contributing.md) |
| Machine-readable index of every doc | [llms.txt](./llms.txt) |

Chinese: [doc/ai/zh-CN/](./doc/ai/zh-CN/).

## The 60-second version

```bash
npm install                                    # Node.js >= 22, this checkout only

# Compile and run (from source; a release archive calls `xbintsc` directly)
npx tsx src/cli/main.ts run examples/hello.ts
npx tsx src/cli/main.ts build examples/hello.ts --out build
./build/hello                                  # build/hello.exe on Windows

# Inspect the generated LLVM IR instead of building
npx tsx src/cli/main.ts emit examples/hello.ts

# Which toolchain did xbintsc pick?
npx tsx src/cli/main.ts doctor
```

A program that needs a Node module must opt the extension in, or the import
fails: `npx tsx src/cli/main.ts run app.ts --ext node`.

## Non-negotiable facts about the language

These are the traps that make plausible-looking TypeScript fail. Confirm details
in [doc/ai/language-support.md](./doc/ai/language-support.md).

- **It is a subset.** Syntax outside the subset fails with `UnsupportedFeature`
  or a parse error. Never assume "TypeScript" means "what Node accepts".
- **Types are erased, never checked.** Annotations, interfaces, generics and
  `as` do nothing at runtime — a type error in the source is not caught for you.
- **Node modules need `--ext node`.** Bare third-party npm imports are not
  supported.
- **Strings are UTF-8 bytes.** `"é".length` is 1, `"😀".length` is 4 — not the
  ECMAScript UTF-16 counts.
- **`async` is a synchronous microtask model,** not a real event loop. A promise
  settled from a timer or socket callback cannot be awaited.
- **No CommonJS.** `require()` is rejected; use ESM `import`. No live bindings.
- **clang ≥ 16 is required.** clang 15 rejects the emitted IR (typed-pointer
  error `'@.str.0' defined with type '[6 x i8]*' but expected 'i8*'`). `xbintsc
  doctor` reports the resolved version.

## Repository map

```
src/lexer, src/parser, src/ast     source text -> tokens -> AST
src/binder                         scopes, symbols, closures
src/codegen                        AST -> LLVM IR text (values.ts defines the value model)
src/driver                         build orchestration, clang invocation, incremental cache
src/extensions                     optional compile modules (node/, gui/, native C++/Rust)
src/diagnostics                    diagnostic codes and rendering
src/cli/main.ts                    the CLI (`build` / `run` / `emit` / `doctor`)
runtime/                           the C runtime: values, containers, stdlib, GC, event loop
tests/                             per-module unit tests + tests/e2e that compile real binaries
doc/                               the detailed, canonical documentation
```

## Rules for changing this repository

- **Keep the source files small.** ESLint enforces a 600-line cap per code file
  (`npm run lint`), so split modules by responsibility instead of growing them.
- **No `any`.** `@typescript-eslint/no-explicit-any` is an error.
- **Run the gate before you claim done:** `npm run typecheck && npm run lint &&
  npm test`. End-to-end tests compile and run real binaries, so they need clang;
  they skip when it is missing.
- **Runtime changes are C.** Compiled functions use one ABI —
  `xt_value fn(xt_value env, int32_t argc, xt_value *argv)` — and every value is
  one NaN-boxed 64-bit word (see `runtime/rt.h`, `src/codegen/values.ts`).
- **Update the docs when support changes.** `doc/implemented.md`,
  `doc/unimplemented.md` and `doc/ai/language-support.md` are the contract AI
  agents rely on; a feature that lands without them becomes a wrong answer later.
- **Bilingual docs.** `doc/*.md` is English and `doc/zh-CN/*.md` is its
  translation; `doc/ai/` follows the same split.
