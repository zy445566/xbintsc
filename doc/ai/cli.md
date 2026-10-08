# CLI and configuration reference

The authoritative `--help` text is the `HELP` constant in
[../../src/cli/main.ts](../../src/cli/main.ts). This page adds the semantics that
the help text leaves implicit.

## Commands

| Command | Purpose | Needs clang? |
| --- | --- | --- |
| `xbintsc build <file.ts>` | Compile to `exe` (default), `obj` or `ir` | yes, except `--emit ir` |
| `xbintsc run <file.ts> [-- args]` | Compile to an executable and execute it | yes (`--emit exe` only) |
| `xbintsc emit <file.ts>` | Print LLVM IR to stdout, write nothing | no |
| `xbintsc doctor` | Report the resolved toolchain, runtime and icon tools | only for the probe |
| `xbintsc version` | Print the version | no |
| `xbintsc help` | Print the help text | no |

Any command also accepts `--help`, which prints the help text and exits 0. An
unknown command prints an error plus the help text and exits 1.

## Options

```
-o, --output <path>   Explicit output path (overrides --out and the config)
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0 .. -O3            Optimization level passed to clang (default: -O2)
    --ext <names>     Enable bundled extensions, comma separated (e.g. node,gui)
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
```

Parsing details that occasionally surprise:

- `--flag=value` and `--flag value` are both accepted.
- `-O`, `-O1`, `-O2`, `-O3` all work; a bare `-O` means `-O2`.
- `--` ends option parsing: everything after it belongs to the program under
  `run`, or is a positional for the other commands.
- `-o` consumes the next argument unconditionally; the other value flags leave
  the value unset when the next argument starts with `-`.

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
