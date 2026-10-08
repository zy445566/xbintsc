# Troubleshooting

Start with the command that shows what xbintsc actually resolved:

```bash
xbintsc doctor
```

It prints the platform, the toolchain it will use (and where it came from), the
compiler version, the runtime directory, the runtime library directory and the
icon resource compiler. Most "it doesn't work" reports are answered here.

## Before anything else: is clang new enough?

**clang/LLVM 16 or newer is required.** Older clang rejects the emitted IR with a
typed-pointer error:

```
D:\...\build\app.ll:382:37: error: '@.str.0' defined with type '[6 x i8]*' but expected 'i8*'
  %r0 = call i64 @xt_string_new(i8* @.str.0, i64 5)
```

The compiler emits LLVM IR that relies on opaque pointers; typed pointers were
removed in LLVM 16 ([LLVM release notes](https://github.com/llvm/llvm-project/blob/release/15.x/llvm/docs/ReleaseNotes.rst#changes-to-the-llvm-ir)).
Confirm with `clang --version`; if the machine has an old system clang, install a
newer LLVM and point xbintsc at it:

```bash
export xbintsc_CLANG=/usr/lib/llvm-18/bin/clang     # PowerShell: $env:xbintsc_CLANG = "C:\Program Files\LLVM\bin\clang.exe"
```

Note that front-end-only work (`xbintsc emit`) never needs clang, so it keeps
working on an old toolchain.

## Error messages and what they mean

### `module '…' is provided by the 'node' extension; pass --ext node`

The program imports a Node module but the extension is off. Add the flag, or put
it in `xbintsc.config.json` so it is always on:

```bash
xbintsc run app.ts --ext node
```

### `module '<pkg>' is not supported: xbintsc can import built-in platform modules, relative '.ts' files and ESM packages under node_modules; CommonJS packages are not supported`

A bare third-party import that nothing can link. Either the package is CommonJS
(unsupported), or it is an ESM package that failed to bundle. Prefer a Node
built-in from the `node` extension; see [extensions.md](./extensions.md) for
what can actually be bundled.

### ``CommonJS `require()` is not supported``

Convert to ESM: `const fs = require("fs")` becomes `import fs from "fs"` (plus
`--ext node`). `require` inside `node_modules` packages *is* handled by the
bundler; it is only rejected in your own source. `require(<non-literal>)` is
never supported.

### `error TS4005: xbintsc does not yet support this <construct>`

The syntax is parsed but codegen does not implement it. Check
[language-support.md](./language-support.md) and
[../unimplemented.md](../unimplemented.md), then rewrite the construct (or
contribute support). `namespace` declarations and `new.target` are the common
ones.

### `error TS2xxx` (parser) or `error TS1xxx` (lexer)

A syntax error in the source. The diagnostic prints the file, line, column, the
offending line and a caret. Note that xbintsc's parser accepts most TypeScript,
so a parse error usually means genuinely broken syntax rather than an
unsupported feature.

### `error TS6001: Cannot resolve module './x' from '<file>'` / `Cannot find module`

A relative import that does not resolve. A `./helper.js` specifier maps to
`helper.ts`, so import the runtime path you actually wrote in TypeScript.

### `Cannot resolve module '…' required from '<file>'`

A `require(...)` inside a bundled `node_modules` package that could not be
resolved. The package is not usable as-is; prefer the built-in or a different
dependency.

### `xbintsc: <config path>: invalid JSON (…)` / `Unable to read …`

The project config or a native extension manifest is malformed or missing. Fix
the JSON, or bypass discovery with `--no-config` / `--config <path>`.

### `error TS6003` / `Command failed (N): clang …`

clang itself failed. The thrown message includes the full clang command and its
stderr — read the stderr, not just the first line. Frequent causes:

- the toolchain is too old (see above);
- on Windows, clang cannot find the MSVC/SDK headers or libraries because the
  environment was not imported. Run from an **x64 Native Tools Command Prompt
  for VS 2022** (or **ARM64 Native Tools** on Windows on ARM), or import it
  first:
  ```powershell
  & "$env:ProgramFiles\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
  ```
- the linker is wrong on Linux — set `xbintsc_CLANG` and/or pass
  `xbintsc_LINKER_ARGS=-fuse-ld=lld`;
- `No C compiler found. Set xbintsc_CLANG to a clang binary.` — no clang on
  `PATH` at all.

### `GUI native library not found at …`

The `gui` extension needs its per-platform prebuilt `gui.a`/`gui.lib`, which is
not built in a plain source checkout. Drop `--ext gui`, or build the archive as
described in [../gui.md](../gui.md).

### `Icon file not found` / icon tools missing

`--icon`/`app.icon` paths resolve against the config file's directory. On
Windows, embedding a PE icon needs `llvm-rc` or `windres`; `doctor` reports which
one was found, and a missing one is only fatal when an icon was requested.

## Build behaviour that looks wrong but is not

### The build printed `(cached)` and did nothing

The incremental cache matched: same source hash, compiler version, options,
platform and extension set, with every output still present. That is the feature
working. Force a rebuild with `--force`.

### Stale output after changing a flag

The object cache key deliberately excludes compiler flags, which is why scripts
that change flags (such as runtime coverage) use a separate
`xbintsc_CACHE_DIR`. If you suspect a stale artefact, pass `--force` or delete
`.xbintsc/`.

### `run` exits with a non-zero status but there is no error message

`run` propagates the **program's** exit status; the compiler succeeded. Run it
directly (`./build/app`) to see the program's own output.

### `xbintsc: run requires --emit exe`

`run` only executes executables. Use `build --emit ir` or `emit` to inspect IR.

### Program output appears in an unexpected order

`async`/`await` is a synchronous microtask model and the event loop only runs
after the program body. Timer and socket callbacks therefore fire late; see
[language-support.md](./language-support.md).

## Where to look in the code

| Symptom | Source |
| --- | --- |
| A diagnostic message or code | [../../src/diagnostics/diagnostic.ts](../../src/diagnostics/diagnostic.ts), emit sites under `src/` |
| A missing feature | [../../src/codegen/](../../src/codegen/) — `UnsupportedFeature` is raised there |
| An import that will not resolve | [../../src/driver/bundler/](../../src/driver/bundler/) |
| clang invocation, linker flags | [../../src/driver/toolchain.ts](../../src/driver/toolchain.ts), [../../src/driver/toolchain-provider.ts](../../src/driver/toolchain-provider.ts) |
| Cache behaviour | [../../src/driver/cache.ts](../../src/driver/cache.ts) |
| Runtime crashes, GC, values | `runtime/*.c`, `runtime/rt.h` |

If you are changing the compiler rather than using it, read
[contributing.md](./contributing.md) next.
