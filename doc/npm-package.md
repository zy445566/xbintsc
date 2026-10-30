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
   Node-compatible TypeScript. With the optional platform package installed the
   first build links a prebuilt runtime; without it, the C runtime is compiled
   from source and that build is noticeably slower.

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

Installing it also installs the optional `@xbintsc/<os>-<arch>` package for the
host (for example `@xbintsc/linux-x64`). That package contains the prebuilt
runtime archives under `runtime/lib/<os>-<arch>/`: `core`, `ext_node` and, where
the release publishes them, `gui` and `media`. The driver links those archives
in place, so the first build does not compile the C runtime and needs no extra
download from the release page.

The package does **not** contain:

- a C compiler — you need clang 16+ on the machine that builds.

If npm skips the optional package (`--omit=optional`, a package manager that
does not install optional dependencies, or a host the release does not build
for), the driver falls back to compiling the C runtime from the shipped sources
and caches the objects; that build works but is slower, and `--ext gui` (plus
the auto-selected `media`/`gpu` link features) needs the archive to be present.

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

Expected output:

```text
xbintsc: wrote /absolute/path/my-app/build/main
Hello from xbintsc
```

`xbintsc build` with no positional entry reads `entry` from
`xbintsc.config.json`, which is why the script stays short. Without a config,
pass the file explicitly: `xbintsc build src/main.ts --out build`.

## The first build and the cache

With the platform package installed, the first build links the prebuilt
`runtime/lib/<os>-<arch>/core.a` (`.lib` on Windows) and does not compile C at
all, so it takes about as long as an incremental build.

If the optional package is missing — `npm install --omit=optional`, a package
manager that does not install optional dependencies, or a host the release does
not build for — the driver compiles the C runtime from the shipped sources
instead. The runtime objects are cached by source/header content hash. The
default cache is `.xbintsc/` **in the directory you run the command from** (not
necessarily next to the config file); set `xbintsc_CACHE_DIR` to put it
elsewhere. Expect that fallback build to take noticeably longer than the ones
after it.

To ignore the platform archive on purpose and compile the runtime from the
sources shipped in the package — for example after patching `runtime/`, or when
the prebuilt archive does not match the environment — set:

```bash
xbintsc_PREFER_PREBUILT=0 npx xbintsc build src/main.ts --out build
```

The objects are still cached in `.xbintsc/`; point `xbintsc_CACHE_DIR` at an
empty directory (or delete `.xbintsc/`) for a full rebuild. `--force` alone
does not bypass a prebuilt archive. The archive files themselves are produced
by `npm run runtime` in a source checkout; users never need to regenerate them
just to compile.

`--time` shows where the time went:

```bash
npx xbintsc build src/main.ts --out build --time
# fallback without the platform package:
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
  directory) so a fallback build does not recompile the runtime every time;
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
- macOS runners already have Xcode Command Line Tools; Windows runners need the
  Visual Studio C++ build tools (MSVC + its bundled LLVM), and the binary is
  `build\main.exe`. In a source checkout,
  `pwsh -File scripts/setup-vs-toolchain.ps1` imports that toolchain explicitly.

## npm package or release archive?

| | npm package (`xbintsc`) | Prebuilt release archive |
| --- | --- | --- |
| Needs Node.js | Yes, 22+ | No |
| First build | Links the prebuilt runtime from the optional platform package | Links the prebuilt runtime |
| GUI engine + media archive | Included on Linux/macOS and Windows x64 (the hosts the release builds them for) | Included in Linux/macOS and Windows x64 archives |
| Installation | `npm install` + lockfile / provenance | Download, verify `.sha256`, unpack |
| Programmatic API | Yes | No (CLI only) |
| Cross-compiles | No — host only | No — host only |
| Best for | Node projects and CI, familiar npm workflow | Machines without Node, GUI apps, a fully self-contained install |

The npm package gets the same prebuilt runtime through its optional platform
package, so the cold-build penalty only applies when that package is skipped.
Use a release archive when Node is not available.

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
| The first build takes a long time | The optional `@xbintsc/<os>-<arch>` package was skipped (or this host has no prebuilt package), so the runtime C sources are compiled and cached. Check `npm ls @xbintsc/<os>-<arch>` and reinstall without `--omit=optional`; `--time` confirms. |
| Permission errors writing `.xbintsc/` | Set `xbintsc_CACHE_DIR` to a writable directory, or run from a writable working directory. |
| `module 'fs' is provided by the 'node' extension` | Add `--ext node`, or `"extensions": ["node"]` in the config. |
| `GUI native library not found` / `media native library not found` | The optional platform package that ships `gui.a`/`gui.lib` or `media.a`/`media.lib` was skipped, or this host has no prebuilt GUI/media archive (Windows arm64). Reinstall without `--omit=optional`, use a Linux/macOS/Windows x64 release archive, or build the archive from a source checkout. An auto-detected `media` feature with a missing archive only warns; `--feature media` makes it an error. |
| `require("xbintsc")` fails | The package is ESM-only; use `import` (or dynamic `import()`). |
| Windows link errors about missing MSVC libraries | Run from a Native Tools prompt, install the VS C++ build tools, and check `npx xbintsc doctor`. |
| `npm ci --omit=dev` then the build cannot find `xbintsc` | Install devDependencies for the build stage, or move `xbintsc` to `dependencies`. |

More error messages and their fixes: [troubleshooting](./ai/troubleshooting.md).
