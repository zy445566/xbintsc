# Contributing to the compiler

Use this page after [../AGENTS.md](../../AGENTS.md), which covers the layout, the
build gate and the rules. This one covers the workflow details that are easy to
get wrong.

## Set up

```bash
npm install
npm run typecheck        # tsc --noEmit
npm run lint             # eslint + the 600-line file budget
npm test                 # unit + end-to-end
```

Node.js ≥ 22 is required to run the compiler from source, and a **clang 16+**
toolchain is required for anything that links a binary (see
[troubleshooting.md](./troubleshooting.md)). `xbintsc emit` needs neither clang
nor the runtime library, which makes it the fastest inner loop.

Optional but recommended — install the repository hook:

```bash
git config core.hooksPath .githooks
```

`.githooks/pre-commit` **bumps the patch version on every commit** and folds
`package.json` + `package-lock.json` into that commit (`npm version patch
--no-git-tag-version`). Expect a version bump in your commits; do not countermand
it. Release tags are created by `release.yml`, never by the hook.

## The test suites

| Command | Scope |
| --- | --- |
| `npm test` | everything: per-module unit tests plus `tests/e2e` |
| `npm run test:e2e` | only the compile-and-run tests |
| `npm run test:watch` | vitest in watch mode |
| `npm run coverage` | V8 coverage of the TypeScript compiler (`src/`) |
| `npm run coverage:runtime` | LLVM coverage of the C runtime (`runtime/`) |

Unit tests live one directory per module under `tests/` (`lexer`, `parser`,
`binder`, `codegen`, `driver`, `extensions`, `cli`, `e2e`) and mirror the source
tree. Edge-case tests belong next to the module they cover.

`tests/e2e/` compiles real programs and runs the resulting binaries, including a
**differential harness** (`tests/e2e/differential-*.test.ts`) that executes the
same source through both xbintsc and Node and compares output byte-for-byte. That
harness is the strongest tool in the repository: when you touch semantics, add a
case there rather than asserting a hand-written expected string. Tests that need
clang skip themselves when it is unavailable (`hasClang()` in
[tests/helpers.ts](../../tests/helpers.ts)) — do not turn that into a silent
pass.

## Two invariants you must not break

### 1. The self-hosting fixpoint

CI (`self-host` job in [.github/workflows/ci.yml](../../.github/workflows/ci.yml))
compiles the compiler with itself and requires the emitted IR to be identical
across generations:

```
source --emit--> ref.ll
source --build--> gen1 binary
gen1 --emit--> gen2.ll      # must equal ref.ll
gen1 --build--> gen2 binary
gen2 --emit--> gen3.ll      # must equal ref.ll
```

By consequence: **any change that alters emitted IR must still be reproducible by
the compiler being changed**, and the IR must be byte-for-byte deterministic —
iteration order, generated symbol names and numbering included. Never introduce
nondeterminism (a `Map`/`Set` iteration over insertion-ordered data you did not
control, a timestamp, a filesystem-order dependency) into codegen.

Verify locally:

```bash
npx tsx src/cli/main.ts emit src/cli/main.ts --ext node > scratch/ref.ll
npx tsx src/cli/main.ts build src/cli/main.ts --ext node --out scratch/self --force
./scratch/self/main emit src/cli/main.ts --ext node > scratch/gen2.ll   # .exe on Windows
diff scratch/ref.ll scratch/gen2.ll
```

### 2. The runtime ABI and value model

- Every compiled function uses
  `xt_value fn(xt_value env, int32_t argc, xt_value *argv)` — direct calls and
  closure calls share this one path, and `env` threads captured variables
  through boxes.
- Every value is a single 64-bit word: doubles unboxed, everything else a tagged
  pointer (16-bit tag + 48-bit payload). The representation is defined **once**
  in [../../src/codegen/values.ts](../../src/codegen/values.ts) and
  [../../runtime/rt.h](../../runtime/rt.h); change both together or not at all.
- Awkward JS semantics (`+` coercion, relational comparison, property access,
  inspection) are delegated to `@xt_*` runtime calls rather than inlined.
- The GC is a non-moving mark-sweep collector: new heap objects go through
  `xt_alloc`, and a value must be reachable from an explicit root slot, a
  registered root provider (the event loop, the microtask queue) or the
  conservative C-stack scan while it is live. A value held only in a C local that
  is not on the scanned stack is a bug.

## Changing the runtime

`runtime/` is C, split by function across translation units (`xt_alloc.c`,
`xt_values.c`, `xt_containers.c`, `xt_stdlib.c`, …) sharing
`runtime/rt_internal.h`. The runtime must be rebuilt for C changes to take
effect:

```bash
npm run runtime            # tsx scripts/build-runtime.ts -> runtime/lib/<os>-<arch>/
npm run runtime:clean      # drop build/runtime-obj and runtime/lib
```

Add a new C file only together with the place that lists the runtime sources
(`RUNTIME_SOURCES` in [../../src/driver/compiler.ts](../../src/driver/compiler.ts)),
because that list decides what gets compiled and linked. Extension C sources are
declared by the extension itself.

## Adding or extending an extension

An extension is a plain object ([../../src/extensions/registry.ts](../../src/extensions/registry.ts))
contributing `runtimeSources()`, `modules()` / `builtins()`, and optionally
`nativeObjects()` or `assetLoaders()`. The Node extension is one folder per
module, pairing its TypeScript exports with the C sources that implement them:

```
src/extensions/node/fs/index.ts        runtime/ext_node/fs/read_file.c
```

Adding a Node module therefore means dropping a folder in each place and
registering it in the module list; the core compiler never changes. If your
feature is optional or platform-specific, it belongs in an extension rather than
in `src/codegen`. Bundled extensions are listed in
[../../src/extensions/catalog.ts](../../src/extensions/catalog.ts) — an extension
listed there but not enabled produces the actionable `pass --ext <name>` hint, so
add modules to the list even when the feature is off by default.

Extension authoring from C++/Rust without touching the compiler is documented in
[../../examples/extensions/README.md](../../examples/extensions/README.md).

## Adding a diagnostic

1. Add a stable code to the enum grouped by pipeline stage in
   [../../src/diagnostics/diagnostic.ts](../../src/diagnostics/diagnostic.ts)
   (1xxx lexer, 2xxx parser, 3xxx binder, 4xxx checker, 5xxx codegen, 6xxx
   driver). Codes are a public interface — never renumber one.
2. Emit it with the most specific range you have; `formatDiagnostic` renders the
   file, line, column, excerpt and caret from it.
3. Make the message actionable, in the style of the existing ones: say what is
   wrong *and* what to do (`pass --ext node`, `use an ESM import`, …). If a
   reader needs more, the CLI hint layer maps codes to a document — extend that
   map in [../../src/cli/main.ts](../../src/cli/main.ts) when you add a code.
4. Cover it in `tests/diagnostics` or the module's own test.

## Documentation is part of the change

A feature is not done until the docs match:

| Document | Update when |
| --- | --- |
| [../implemented.md](../implemented.md) | a feature starts working |
| [../unimplemented.md](../unimplemented.md) | a limitation appears or is lifted |
| [../node-implemented.md](../node-implemented.md) / [../node-unimplemented.md](../node-unimplemented.md) | Node module coverage changes |
| [language-support.md](./language-support.md) | the AI-facing summary of the above changes |
| [../requirements.md](../requirements.md) | the toolchain or platform requirements change |
| [cli.md](./cli.md) | a flag, config field or environment variable changes |

Keep the Chinese translation (`doc/zh-CN/`, `doc/ai/zh-CN/`) in step with the
English source; a half-translated page is worse than none because it silently
goes stale.

## Repository hygiene

- **600 lines per code file**, enforced by ESLint (`max-lines`) for TS/JS and by
  [scripts/check-file-length.ts](../../scripts/check-file-length.ts) for C, C++,
  Rust, `.inc` and shell files. Split by responsibility instead of growing a
  file; vendored sources are exempt.
- **No `any`**; `prefer-const` and `eqeqeq` are errors. The
  declaration-merging pattern in the parser/generator is deliberately allowed.
- **Public API changes** go through [../../src/index.ts](../../src/index.ts) and
  must keep the `xbintsc` / `xbintsc/driver` export maps in
  [../../package.json](../../package.json) valid.
- `npm run package-release` assembles the per-platform archives into
  `dist/release/`; the release workflow owns tagging, so never push a tag by
  hand.
