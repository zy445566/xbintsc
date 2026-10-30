# CLI and programmatic API

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/cli.md)

Location: `src/cli/main.ts`, `src/index.ts`, `bin/xbintsc.js`

## CLI commands

```
xbintsc build <file.ts> [options]   compile to a native binary
xbintsc run   <file.ts> [-- args]   compile and run
xbintsc check <file.ts>             parse, bind and check without linking
xbintsc watch <file.ts>             rebuild on source-tree changes
xbintsc emit  <file.ts>             print LLVM IR
xbintsc doctor                      report the resolved toolchain
xbintsc version                     print the version
xbintsc help                        help
```

`check`, `emit`, `build`, `run` and `watch` all run the semantic checker by
default; `--no-check` skips it (and is part of the cache key). See
[the checker page](./checker.md).

## CLI options

```
-o, --output <path>   Output path
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0..-O3              Optimization level (default: -O2)
    --ext <names>     Comma separated extensions (e.g. node)
    --feature <names> Force optional link features on (media,gpu,images,webgl,all)
    --no-feature <n>  Keep optional link features out even when detected
    --ext-native <m>  Register a C++/Rust extension from a JSON manifest
    --config <path>   Use a project config (default: xbintsc.config.json)
    --no-config       Do not read any project config
    --icon <path>     Embed an application icon (PNG/ICO/ICNS)
    --bundle          macOS: also produce a .app bundle
    --app-name <name> Bundle / display name
    --app-id <id>     macOS bundle identifier (e.g. com.example.demo)
    --force           Ignore the incremental cache
    --verbose         Print progress information
    --debug           Emit DWARF/CodeView debug info
    --check           Run the semantic checker (default)
    --no-check        Skip the semantic checker
```

## Programmatic API

```ts
import { build, compileString } from "xbintsc";

const { ir, diagnostics } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
// The checker can be disabled per call, exactly like `--no-check`:
build("program.ts", { check: false });
```

The option and result types (`BuildOptions`, `BuildResult`, `EmitKind`,
`PhaseTiming`) live in `src/driver/options.ts` and are re-exported from
`src/driver/compiler.ts` / `xbintsc`.
