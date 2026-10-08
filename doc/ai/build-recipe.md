# Build recipe

Copy-paste shapes for the common jobs, plus the reasoning that makes a build
succeed. For every flag see [cli.md](./cli.md); when a command fails see
[troubleshooting.md](./troubleshooting.md).

## Calling the compiler

| Situation | Invocation |
| --- | --- |
| Released standalone archive | `xbintsc …` (`bin/xbintsc[.exe]` on `PATH`) |
| Source checkout, compiled | `node dist/src/cli/main.js …` |
| Source checkout, TypeScript sources | `npx tsx src/cli/main.ts …` |
| Source checkout, npm script | `npm run xbintsc -- …` |
| `bin` launcher in a checkout | `node bin/xbintsc.js …` (falls back to `tsx` automatically) |

The rest of these pages write `xbintsc` for brevity.

## Run a program

```bash
xbintsc run app.ts
xbintsc run app.ts -- --flag value        # everything after -- goes to the program
```

`run` compiles the program to an executable and then spawns it with inherited
stdio. It requires `--emit exe`; `xbintsc run app.ts --emit ir` is an error.
The exit status of `run` is the exit status of the program it ran.

## Build a standalone binary

```bash
xbintsc build app.ts                      # -> build/app (build/app.exe on Windows)
xbintsc build app.ts --out build/app      # choose the output directory
xbintsc build app.ts -o app.bin            # or an explicit output path
xbintsc build app.ts -O0                   # optimization level: -O0 .. -O3 (default -O2)
xbintsc build app.ts --force               # ignore the incremental cache
xbintsc build app.ts --verbose             # print progress
```

`build` prints the artifact it wrote, and `(cached)` when the incremental cache
made the work unnecessary:

```
xbintsc: wrote /abs/path/build/app
```

Other emit kinds are useful for inspection:

```bash
xbintsc build app.ts --emit ir   # writes app.ll
xbintsc build app.ts --emit obj  # writes app.o
```

## Inspect the LLVM IR

```bash
xbintsc emit app.ts > app.ll     # IR on stdout, nothing else written
```

`emit` needs no clang and no runtime library — it is the cheapest way to check
what the compiler understood. It is the fastest feedback loop when you are
unsure whether a construct is supported.

## Program shape that compiles

```ts
// app.ts — ESM, no require()
import { readFileSync } from "fs";          // needs `--ext node`

function main(): void {
  const text = readFileSync("package.json", "utf8");
  console.log(text.length);
}

main();                                     // top-level code runs in order
```

- Use `import`/`export`; `require()` is rejected.
- Relative imports are bundled (`import { helper } from "./helper.js"` resolves
  to `helper.ts`).
- Module specifiers for Node built-ins are the bare names (`fs`, `path`,
  `node:fs`), and they need the `node` extension enabled.

## Project config: build with no arguments

`xbintsc.config.json` (discovered by walking up from the entry file, or selected
with `--config <path>`; disable with `--no-config`) holds the build options, so
`xbintsc build` alone works. Paths resolve against the config file's directory,
and any CLI flag overrides the matching field.

```json
{
  "entry": "src/app.ts",
  "outDir": "build",
  "optimize": "2",
  "extensions": ["node"],
  "app": { "name": "Demo", "icon": "assets/app.png" }
}
```

The machine-readable schema is [../xbintsc.config.schema.json](../xbintsc.config.schema.json);
the full field list is in [cli.md](./cli.md).

## Compiling with extensions

An import of a Node module fails until the extension is enabled. Enable one or
several, comma-separated:

```bash
xbintsc run app.ts --ext node
xbintsc run app.ts --ext node,gui
```

A C++/Rust library is added by manifest instead:

```bash
xbintsc run app.ts --ext-native ./mathx.manifest.json
```

Details, including how to author both kinds, are in
[extensions.md](./extensions.md).

## Programmatic API

When you are writing tooling rather than a program:

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");   // IR text, no clang needed
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

`build` returns `{ outputPath, irPath?, cached, diagnostics, bundlePath? }` and
reports recoverable problems through `diagnostics` rather than throwing. The
subpath export `xbintsc/driver` exposes the driver internals.
