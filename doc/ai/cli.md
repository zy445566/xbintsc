# CLI and configuration reference

The authoritative `--help` text is the `HELP` constant in
[../../src/cli/main.ts](../../src/cli/main.ts). This page adds the semantics that
the help text leaves implicit.

## Commands

| Command | Purpose | Needs clang? |
| --- | --- | --- |
| `xbintsc build <file.ts>` | Compile to `exe` (default), `obj` or `ir` | yes, except `--emit ir` |
| `xbintsc run <file.ts> [-- args]` | Compile to an executable and execute it | yes (`--emit exe` only) |
| `xbintsc watch <file.ts>` | Rebuild whenever the source tree changes (Ctrl+C to stop) | yes |
| `xbintsc emit <file.ts>` | Print LLVM IR to stdout, write nothing | no |
| `xbintsc check <file.ts>` | Front-end check (syntax, imports, semantic checks, supported subset); writes nothing | no |
| `xbintsc doctor` | Report the resolved toolchain, runtime and icon tools | only for the probe |
| `xbintsc version` | Print the version | no |
| `xbintsc help` | Print the help text | no |

`check` is not a full type checker: annotations are erased after checking, so a
program that passes `check` can still fail at runtime. It catches syntax errors,
unresolved imports, constructs outside the supported subset, and the semantic
checker's diagnostics: unresolved names (TS3002), same-scope redeclarations
(TS3001), call-arity mismatches (TS4004), missing properties on known shapes
(TS4003), definite assignability mismatches (TS4001) and calls to non-callable
values (TS4002). That makes it a fast pre-commit or editor gate. `--no-check`
skips the semantic part; see [the checker page](../implemented/checker.md).

The checker is on by default for **every** command (`build`, `run`, `emit`,
`watch` and `check`). The flag is part of the incremental cache key, so a
`--no-check` build never reuses a checked artifact. Errors are reported before
clang runs, which is what makes `xbintsc check` cheap in an editor or a
pre-commit hook.

Any command also accepts `--help`, which prints the help text and exits 0. An
unknown command prints an error plus the help text and exits 1.

`watch` polls the entry file's directory (build output, `dist`, `coverage` and
`node_modules` are ignored) and re-runs `build` on every change. It accepts the
same options as `build`; package sources under `node_modules` are not watched,
so restart it after installing a dependency.

## Options

```
-o, --output <path>   Explicit output path (overrides --out and the config)
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0 .. -O3            Optimization level passed to clang (default: -O2)
    --ext <names>     Enable bundled extensions, comma separated (e.g. node,gui,media)
    --ext-native <m>  Register a C++/Rust extension from a JSON manifest
                      (comma separated for several)
    --config <path>   Use this project config instead of discovering one
    --no-config       Do not read any project config
    --icon <path>     Embed an application icon (PNG/ICO/ICNS)
    --bundle          macOS: also produce a <name>.app bundle
    --app-name <name> Bundle / display name
    --app-id <id>     macOS bundle identifier (e.g. com.example.demo)
    --force           Ignore the incremental cache
    --verbose         Print progress information
    --time            Print per-phase build timings
    --debug           Emit debug info (DWARF/CodeView) and keep frame pointers
    --check           Run the semantic checker (default)
    --no-check        Skip the semantic checker; only erase type syntax
    --lto             Compile program and runtime to LTO bitcode; link with lld
    --pgo-generate [dir]  Build an instrumented binary writing profiles to dir
    --pgo-use [dir]   Build with the profiles collected from --pgo-generate
    --json            Print diagnostics as a JSON array on stdout
```

### LTO and PGO

`--lto` compiles the generated IR and the C runtime to LLVM bitcode (`-flto`)
and links with lld (`-fuse-ld=lld`), so LLVM can optimize across the
language/runtime boundary. lld must be installed alongside clang.

PGO is a two-build workflow:

```bash
xbintsc build app.ts --pgo-generate=profiles   # instrumented binary
./build/app                                    # run a representative workload
xbintsc build app.ts --pgo-use=profiles        # optimized rebuild
```

`--pgo-use` merges the `.profraw` files with `llvm-profdata` (the copy next to
clang is preferred; override with `xbintsc_LLVM_PROFDATA`) and passes
`-fprofile-use=<dir>` to clang. The profile directory defaults to
`<outDir>/pgo`. LTO and PGO can be combined. A `--pgo-use` build is never served
from the incremental cache, because the profile data can change independently
of the source.

Parsing details that occasionally surprise:

- `--flag=value` and `--flag value` are both accepted.
- `-O`, `-O1`, `-O2`, `-O3` all work; a bare `-O` means `-O2`.
- `--` ends option parsing: everything after it belongs to the program under
  `run`, or is a positional for the other commands.
- `-o` consumes the next argument unconditionally; the other value flags leave
  the value unset when the next argument starts with `-`.
- `-o`/`output` may name a directory that does not exist yet: the driver creates
  the parent directories of the output path. (`--out` likewise creates its
  directory.) Without that, an explicit path such as `-o dist/app.exe` where
  `dist/` was missing used to fail inside the linker, since clang — not xbintsc —
  is what writes the executable.

## Project config

`xbintsc.config.json` is discovered by walking **up** from the entry file's
directory (or from the current directory when no entry was given). Every path in
the file resolves relative to the config file, and any CLI flag overrides the
matching field. `--no-config` skips discovery entirely.

| Field | Type | Meaning |
| --- | --- | --- |
| `entry` | string | Entry TypeScript file, so `xbintsc build` needs no positional |
| `outDir` | string | Output directory (default `build/`) |
| `output` | string | Explicit output path, overriding `outDir` |
| `optimize` | `"0"｜"1"｜"2"｜"3"` | Optimization level |
| `lto` | boolean | Compile program and runtime to LTO bitcode and link with lld |
| `pgo` | `"generate"｜"use"` | Profile-guided optimization stage |
| `pgoDir` | string | Profile directory (default `<outDir>/pgo`) |
| `extensions` | string[] | Bundled extensions to enable, e.g. `["node"]` |
| `extNative` | string[] | Native extension manifest paths |
| `force` | boolean | Ignore the incremental cache |
| `app.name` / `app.icon` / `app.bundle` / `app.bundleId` | | Application metadata ([icon.md](../icon.md)) |

JSON schema: [xbintsc.config.schema.json](../xbintsc.config.schema.json). An
unreadable or malformed config fails the build with `invalid JSON` or
`Unable to read` rather than silently falling back to defaults.

## Environment variables

| Variable | Effect |
| --- | --- |
| `xbintsc_CLANG` | Use this clang instead of resolving one from `PATH` |
| `xbintsc_LINKER_ARGS` | Extra linker flags (e.g. `-fuse-ld=lld`) |
| `xbintsc_LLVM_PROFDATA` | `llvm-profdata` used to merge PGO `.profraw` files |
| `xbintsc_CACHE_DIR` | Object-cache directory (default: `.xbintsc`) |
| `xbintsc_PREFER_PREBUILT` | `0` forces the runtime to be compiled from source |
| `xbintsc_BINARY` | Native compiler binary the `bin/` launcher should run |

## Incremental compilation

Each build is keyed on the entry source hash, the compiler version, the build
options, the platform and the active extension set
([../../src/driver/cache.ts](../../src/driver/cache.ts)). If every recorded
output still exists, the build returns immediately and prints `(cached)`. The C
runtime and extension sources are cached the same way; objects live in
`.xbintsc/` (or `xbintsc_CACHE_DIR`).

`--force` (or `force: true`) bypasses the freshness check. If a build ever
returns stale output, that key is the first thing to inspect — and note that the
object cache key deliberately excludes compiler flags, which is why the runtime
coverage script uses a separate cache directory.

## Exit codes

- `0` — success (for `run`: the program exited 0).
- `1` — diagnostics, a usage error, or a failure in the compiler itself.
- `run` otherwise propagates the program's own exit status.

## Diagnostics

Errors are rendered as `file:line:col - error TS<code>: <message>` with a source
excerpt and caret underline. Codes are grouped by stage
([../../src/diagnostics/diagnostic.ts](../../src/diagnostics/diagnostic.ts)):

| Range | Stage |
| --- | --- |
| TS1xxx | lexer |
| TS2xxx | parser |
| TS3xxx | binder |
| TS4xxx | checker (`TS4005` is the `UnsupportedFeature` code) |
| TS5xxx | codegen |
| TS6xxx | driver: `TS6001` module not found, `TS6002` IO, `TS6003` toolchain, `TS6004` cache |

The CLI appends a `hint:` line pointing at the relevant document (for example
[language-support.md](./language-support.md) for `TS4005`, or
[troubleshooting.md](./troubleshooting.md) for `TS6001`/`TS6003`). Toolchain and
IO failures throw instead of producing a diagnostic, so the CLI reports those as
`xbintsc: <message>` plus the same style of hint.

## Programmatic API

```ts
import { build, compileString } from "xbintsc";
```

- `compileString(source, fileName?, extensions?)` → `{ ir, diagnostics }`.
  Pure IR generation: no filesystem, no clang.
- `build(entryPath, options?)` → `BuildResult`. `options` accepts `output`,
  `outDir`, `emit`, `optimize`, `force`, `verbose`, `extensions` (a registry),
  `app`, `clang`, `preferPrebuilt`.
- `canonicalize` of results: `{ outputPath, irPath?, cached, diagnostics, ir?,
  bundlePath? }`.

`build` reports source-level problems through `diagnostics` — always check
`diagnostics.some((d) => d.category === "error")` before using the artifact — but
**throws** `ToolchainError` when clang itself fails
([../../src/driver/toolchain.ts](../../src/driver/toolchain.ts)). The
`xbintsc/driver` subpath additionally exports the cache, toolchain resolution,
config loader, icon and macOS-bundle helpers.
