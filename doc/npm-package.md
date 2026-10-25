# Using xbintsc from npm

> Language: **English** | [简体中文](./zh-CN/npm-package.md)

`xbintsc` is published on npm as a Node package. Install it in any Node project
to compile that project's TypeScript into a standalone native binary — no source
checkout, no downloaded release archive, and no global install required.

This page is the consumer guide for the npm package. For the compiler's flags
and config fields see [the CLI reference](./ai/cli.md); for the platform
toolchain see [requirements](./requirements.md).

## Quick start

```bash
npm install --save-dev --save-exact xbintsc
npx xbintsc doctor                  # "status : ok" means clang is usable
npx xbintsc build src/main.ts --out build
./build/main                        # .\build\main.exe on Windows
```

The rest of this page explains the requirements, the first-build cost, CI and
the differences from a normal Node build.

## For AI agents

If you are an agent asked to build a project that already depends on `xbintsc`:

1. Run `npx xbintsc doctor`. If it exits non-zero, report the toolchain problem;
   do not edit the program to work around a missing or old clang.
2. Find the entry file: `entry` in `xbintsc.config.json`, a package script that
   invokes `xbintsc`, or the project's main TypeScript file (often
   `src/main.ts`). When a config exists, `npx xbintsc build` needs no arguments.
3. Build with `npx xbintsc build <entry> --out build`. If the program imports
   Node built-ins, add `--ext node`; the diagnostic names the missing extension.
4. Run the artifact (`./build/<name>`, `.exe` on Windows) and report its path.
5. If the build reports unsupported syntax, read the installed subset guide at
   `node_modules/xbintsc/doc/ai/language-support.md`; do not assume
   Node-compatible TypeScript. The first build also compiles the C runtime, so
   allow more time than for an incremental build.

Do not run `npm install` inside `node_modules/xbintsc`, and do not substitute
`tsc`/`esbuild` for xbintsc: the native artifact must come from `npx xbintsc`.
`xbintsc` does not read `tsconfig.json`; use relative imports or package imports.

## What the npm package is

The package contains:

- the compiler CLI (`bin/xbintsc.js` plus the compiled JavaScript in `dist/`);
- the C runtime **sources** (`runtime/`), including the `node` extension sources;
- the project config schema and documentation.

It runs the compiler through Node.js. The binary it produces is still
standalone: it does not embed Node, V8 or a TypeScript compiler.

The package does **not** contain:

- a C compiler — you need clang 16+ on the machine that builds;
- prebuilt runtime archives (`runtime/lib/<os>-<arch>/core.a`). The driver
  compiles the C runtime from the shipped sources on first use and caches the
  objects, so the first build is slower (see [the cache](#the-first-build-and-the-cache));
- the GUI/media native archives (`gui.a`, `media.a`). If you need `--ext gui` or
  `--ext media`, use a Linux/macOS release archive or build those archives from
  a source checkout.

## Requirements

| Requirement | Why |
| --- | --- |
| Node.js **22+** | Runs the compiler itself. The produced binary does not need Node. |
| clang / LLVM **16+** | xbintsc emits LLVM IR and asks clang to compile and link it. |
| The platform linker / SDK | macOS: Xcode Command Line Tools. Linux: clang + lld. Windows: LLVM + the Visual Studio C++ build tools (MSVC ABI). |

Check them before the first build:

```bash
node --version          # v22.x or newer
npx xbintsc doctor      # "status : ok" means the toolchain is usable
```

`doctor` also reports the resolved compiler, runtime directory and runtime
library directory. On Windows, if the resolved clang cannot find the MSVC/SDK
headers by itself, run the build from an **x64 Native Tools Command Prompt for
VS 2022** (or **ARM64 Native Tools** on Windows on ARM). On distributions
whose default `clang` is older than 16 (for example Ubuntu 22.04 or Debian 12),
install a newer LLVM from its LLVM package repository and point
`xbintsc_CLANG` at that binary. Full per-platform instructions are in
[requirements](./requirements.md).

## Install it in the project

Install it as a development dependency in the Node project that will run the
build:

```bash
npm install --save-dev --save-exact xbintsc
```

`--save-exact` pins the compiler version in `package.json`; the lockfile pins it
for everyone else. Pinning matters because the compiler version is part of the
incremental-cache key and a newer compiler can produce a different binary.

The equivalent commands for other package managers are `pnpm add -D xbintsc`
and `yarn add -D xbintsc`. The rest of this page uses npm and `npx`.

Verify the local install:

```bash
npx xbintsc version
npx xbintsc doctor
```

`npx` prefers `node_modules/.bin/xbintsc`. If the project has no local install,
`npx` may download the latest version instead — convenient for a one-off, but
not reproducible. In CI, either install the dependency first or use
`npx --no-install xbintsc …` so a missing install fails instead of silently
fetching a different compiler.

### One-off run without adding a dependency

```bash
npx --yes xbintsc@latest build src/main.ts --out build
```

Replace `latest` with an exact version if the artifact must be reproducible.
This downloads the package into npm's `_npx` cache, which is why a project
dependency is preferred for regular builds.

### Global install (not recommended)

```bash
npm install --global xbintsc
xbintsc build src/main.ts --out build
```

A global install is fine for experiments, but every project then shares whichever
version happens to be global. A project devDependency keeps builds reproducible.

## A complete minimal project

```text
my-app/
├── package.json
├── xbintsc.config.json
└── src/
    └── main.ts
```

`src/main.ts`:

```ts
const greeting: string = "Hello from xbintsc";
console.log(greeting);
```

`xbintsc.config.json`:

```json
{
  "entry": "src/main.ts",
  "outDir": "build"
}
```

Add a build script to `package.json` — npm puts `node_modules/.bin` on `PATH`,
so the script can call `xbintsc` directly:

```json
{
  "scripts": {
    "build:native": "xbintsc build"
  }
}
```

Then:

```bash
npm install --save-dev --save-exact xbintsc
npm run build:native
./build/main          # .\build\main.exe on Windows
```

Expected output (the first build also compiles the C runtime):

```text
xbintsc: wrote /absolute/path/my-app/build/main
Hello from xbintsc
```

`xbintsc build` with no positional entry reads `entry` from
`xbintsc.config.json`, which is why the script stays short. Without a config,
pass the file explicitly: `xbintsc build src/main.ts --out build`.

## The first build and the cache

The runtime objects are cached by source/header content hash. The default cache
is `.xbintsc/` **in the directory you run the command from** (not necessarily
next to the config file); set `xbintsc_CACHE_DIR` to put it elsewhere.

Because the npm package ships the runtime as C sources but not as a prebuilt
`core.a`, the first build compiles each runtime translation unit. Expect that
build to take noticeably longer than the ones after it. `--time` shows where the
time went:

```bash
npx xbintsc build src/main.ts --out build --time
# xbintsc: timing (frontend 4ms, codegen 7ms, clang 1089ms, runtime 37770ms, link 1966ms)
```

Later builds reuse the objects and the incremental build record:

```bash
npx xbintsc build src/main.ts --out build
# xbintsc: wrote /absolute/path/build/main (cached)
```

Practical consequences:

- add `.xbintsc/` to `.gitignore`;
- keep `.xbintsc/` between CI runs (or point `xbintsc_CACHE_DIR` at a persistent
  directory) to avoid paying the runtime-compile cost every time;
- deleting `.xbintsc/` is safe — the next build recreates it;
- `--force` ignores the build freshness record, but cached runtime objects are
  still reused. Delete `.xbintsc/` if you want to force a full C rebuild.

## Commands you will use

All commands below assume a local install, so `npx` finds it.

| Job | Command |
| --- | --- |
| Build an executable | `npx xbintsc build src/main.ts --out build` |
| Choose the exact output path | `npx xbintsc build src/main.ts -o dist/app` |
| Compile and run immediately | `npx xbintsc run src/main.ts -- --flag value` |
| Pass arguments to the program | Put them after `--`; they are not compiler flags |
| Check without linking (no clang) | `npx xbintsc check src/main.ts` |
| Print LLVM IR (no clang) | `npx xbintsc emit src/main.ts > main.ll` |
| Emit an object file | `npx xbintsc build src/main.ts --emit obj --out build` |
| Optimize | `npx xbintsc build src/main.ts -O3 --out build` |
| Use Node built-ins | `npx xbintsc build src/main.ts --ext node --out build` |
| Rebuild on change | `npx xbintsc watch src/main.ts` |
| Inspect the toolchain | `npx xbintsc doctor` |
| Read the exact help text | `npx xbintsc help` |

`--out <dir>` is a directory: the output keeps the entry file's base name.
`-o <path>` is the exact output path. Optimization defaults to `-O2`.

The full flag list, project config fields and environment variables are in
[the CLI reference](./ai/cli.md).

## Node built-ins and npm dependencies in the compiled program

Installing `xbintsc` from npm does **not** make the compiled program run on
Node. The output is a standalone native binary, so two rules apply:

1. **Node built-ins are opt-in.** An import such as `node:fs` or `fs` needs the
   `node` extension:

   ```bash
   npx xbintsc build src/main.ts --ext node --out build
   ```

   or, permanently, in `xbintsc.config.json`:

   ```json
   {
     "entry": "src/main.ts",
     "outDir": "build",
     "extensions": ["node"]
   }
   ```

2. **Third-party npm packages are bundled at build time.** A bare import is
   resolved from the project's `node_modules` and its source is merged into the
   binary. There is no runtime package lookup. ESM and CommonJS packages are
   supported; circular dependencies, dynamic `require(<expression>)` and
   packages that rely on Node globals such as `__dirname` are not.

   ```bash
   npm install ms
   ```

   ```ts
   import ms from "ms";
   console.log(ms(60000));
   ```

For the complete rules and limitations see
[extensions](./ai/extensions.md#third-party-npm-packages).

## Build from a Node script

The package is ESM-only and exposes a programmatic API, which is useful when
another Node build script needs to produce the binary:

```js
// scripts/build-native.mjs
import { build } from "xbintsc";

const result = build("src/main.ts", { outDir: "build", optimize: "2" });
if (result.diagnostics.some((diagnostic) => diagnostic.category === "error")) {
  process.exitCode = 1;
} else {
  console.log(`wrote ${result.outputPath}${result.cached ? " (cached)" : ""}`);
}
```

Run it with `node scripts/build-native.mjs` (or a `package.json` script). Notes:

- use `.mjs` or `"type": "module"`, because `require("xbintsc")` is not
  exported and fails with `ERR_PACKAGE_PATH_NOT_EXPORTED`;
- `build` returns `{ outputPath, irPath?, cached, diagnostics, … }` and reports
  source errors through `diagnostics`; it **throws** `ToolchainError` when clang
  itself fails;
- `compileString(source)` is a pure IR-generation call that needs no clang;
- the subpath `xbintsc/driver` exposes the driver internals (cache, toolchain,
  config, icons, macOS bundle helpers).

The full API surface is documented in [the CLI reference](./ai/cli.md#programmatic-api).

## Use it in CI

A GitHub Actions job that publishes the Linux binary can look like this:

```yaml
name: native
on: [push]
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: 22
          cache: npm
      - run: sudo apt-get update && sudo apt-get install -y clang lld
      - run: npm ci
      - run: npx xbintsc doctor
      - uses: actions/cache@v4
        with:
          path: .xbintsc
          key: xbintsc-${{ runner.os }}-${{ hashFiles('package-lock.json') }}
      - run: npx xbintsc build src/main.ts --out build
      - uses: actions/upload-artifact@v4
        with:
          name: my-app-linux-x64
          path: build/main
```

- `npm ci` installs devDependencies by default, so `xbintsc` is present. If your
  pipeline uses `npm ci --omit=dev`, build before pruning or move `xbintsc` to
  `dependencies`.
- The `.xbintsc` cache key includes the lockfile, which pins the compiler
  version; object names inside the cache are content hashes, so an old cache
  cannot poison a new build.
- xbintsc builds for the **host** platform. It is not a cross-compiler: use a CI
  matrix (one job per OS/arch) to produce artifacts for several platforms.
- macOS runners already have Xcode Command Line Tools; Windows runners need
  LLVM and the MSVC build tools, and the binary is `build\main.exe`.

## npm package or release archive?

| | npm package (`xbintsc`) | Prebuilt release archive |
| --- | --- | --- |
| Needs Node.js | Yes, 22+ | No |
| First build | Compiles the C runtime from source | Links the prebuilt runtime |
| GUI / media extensions | Not included | Included in Linux/macOS archives |
| Installation | `npm install` + lockfile / provenance | Download, verify `.sha256`, unpack |
| Programmatic API | Yes | No (CLI only) |
| Cross-compiles | No — host only | No — host only |
| Best for | Node projects and CI, familiar npm workflow | Machines without Node, fastest first build, GUI apps |

If you only need the core runtime and the `node` extension, the npm package is
the most convenient option. If you need the fastest possible cold build or the
GUI/media archives, use a release archive (or build from source for
development).

## Things that differ from a regular Node build

- **`tsconfig.json` is not read.** xbintsc has its own `xbintsc.config.json` for
  the entry file, output directory and build flags. TypeScript `paths` aliases
  and other `tsconfig.json` settings are not applied; use relative imports or
  real package imports.
- **The project's `package.json` `"type"` does not change the source dialect.**
  Write ESM `import`/`export`; `require()` in your own source is rejected.
- **Types are checked, then erased.** The default checker catches definite
  mismatches, but there is no full type inference or strict-null mode. Use
  `--no-check` only when you need to skip the semantic check.
- **Only a subset of TypeScript compiles.** Unsupported syntax fails with a
  diagnostic instead of being approximated. See
  [language support](./ai/language-support.md).
- **The produced binary is platform- and architecture-specific.** Build it on
  the platform you will run it on.

## Troubleshooting

Diagnostic hints end with a repository-relative path such as
`doc/ai/troubleshooting.md`. In a project that installed xbintsc from npm, read
the same file under `node_modules/xbintsc/doc/ai/`.

| Symptom | Cause / fix |
| --- | --- |
| `npx xbintsc` uses an unexpected version | Run `npx --no-install xbintsc version` and `npm ls xbintsc`; pin the version in `package.json` and use `npm ci` in CI. |
| `requires Node.js 22` / engine error | Upgrade Node; the compiler uses modern Node APIs. |
| `No C compiler found` or `requires clang 16 or newer` | Install clang 16+ and run `npx xbintsc doctor`; set `xbintsc_CLANG` to a specific clang if needed. |
| The first build takes a long time | Expected: the runtime C sources are compiled and cached. Later builds reuse `.xbintsc/`; `--time` confirms. Use a release archive if cold builds must be fast. |
| Permission errors writing `.xbintsc/` | Set `xbintsc_CACHE_DIR` to a writable directory, or run from a writable working directory. |
| `module 'fs' is provided by the 'node' extension` | Add `--ext node`, or `"extensions": ["node"]` in the config. |
| `GUI native library not found` / `media native library not found` | The npm package does not ship `gui.a`/`media.a`; use a Linux/macOS release archive or build them from a source checkout. |
| `require("xbintsc")` fails | The package is ESM-only; use `import` (or dynamic `import()`). |
| Windows link errors about missing MSVC libraries | Run from a Native Tools prompt, install the VS C++ build tools, and check `npx xbintsc doctor`. |
| `npm ci --omit=dev` then the build cannot find `xbintsc` | Install devDependencies for the build stage, or move `xbintsc` to `dependencies`. |

More error messages and their fixes: [troubleshooting](./ai/troubleshooting.md).
