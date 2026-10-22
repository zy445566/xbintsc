# CLI and programmatic API

> Part of [Implemented syntax and features](../implemented.md) · [简体中文](../zh-CN/implemented/cli.md)

Location: `src/cli/main.ts`, `src/index.ts`, `bin/xbintsc.js`

## CLI commands

```
xbintsc build <file.ts> [options]   compile to a native binary
xbintsc run   <file.ts> [-- args]   compile and run
xbintsc emit  <file.ts>             print LLVM IR
xbintsc version                     print the version
xbintsc help                        help
```

## CLI options

```
-o, --output <path>   Output path
    --out <dir>       Output directory (default: build/)
    --emit <kind>     exe | obj | ir (default: exe)
-O0..-O3              Optimization level (default: -O2)
    --ext <names>     Comma separated extensions (e.g. node)
    --ext-native <m>  Register a C++/Rust extension from a JSON manifest
    --config <path>   Use a project config (default: xbintsc.config.json)
    --no-config       Do not read any project config
    --icon <path>     Embed an application icon (PNG/ICO/ICNS)
    --bundle          macOS: also produce a .app bundle
    --app-name <name> Bundle / display name
    --app-id <id>     macOS bundle identifier (e.g. com.example.demo)
    --force           Ignore the incremental cache
    --verbose         Print progress information
```

## Programmatic API

```ts
import { build, compileString } from "xbintsc";

const { ir } = compileString("console.log(1 + 1);");
const result = build("program.ts", { emit: "exe", outDir: "build" });
```

The option and result types (`BuildOptions`, `BuildResult`, `EmitKind`,
`PhaseTiming`) live in `src/driver/options.ts` and are re-exported from
`src/driver/compiler.ts` / `xbintsc`.
