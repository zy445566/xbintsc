# Extensions: Node modules, GUI, C++/Rust libraries

The compiler core is platform-agnostic. Anything platform-specific — Node's
`fs`, an HTML/CSS renderer, your own C++ library — arrives as an **extension**
that contributes module bindings plus the C/C++ sources or objects to link.

## The rule that trips people up

**An import of an extension-provided module fails until the extension is
enabled.** xbintsc knows the module exists and says exactly what to pass:

```bash
$ xbintsc run app.ts            # app.ts: import { readFileSync } from "fs";
error TS6001: module 'fs' is provided by the 'node' extension; pass --ext node
```

```bash
xbintsc run app.ts --ext node
```

Enable several at once with a comma-separated list: `--ext node,gui`. In a
project, put them in `xbintsc.config.json` so no flag is needed:

```json
{ "entry": "src/app.ts", "outDir": "build", "extensions": ["node"] }
```

## Bundled extensions

### `node` — Node built-in modules

Enable with `--ext node`. A module is importable by its bare name or the
`node:` prefix (`import { readFileSync } from "node:fs"`), and both spellings
resolve to the same implementation.

| Module | Notes |
| --- | --- |
| `fs` | **synchronous API only** (`readFileSync`, `writeFileSync`, …); `fs/promises` is separate |
| `fs/promises` | promise-based file APIs |
| `path` | also hooks namespace dispatch, so `path.join(...)` works |
| `os` | |
| `process` | `process.cwd()`, `argv`, `env`, …; namespace dispatch too |
| `buffer` | |
| `crypto` | |
| `stream`, `stream/promises` | |
| `events` | |
| `net`, `dgram`, `http` | sockets and servers on the xbintsc event loop (see the async caveat below) |
| `child_process` | |
| `worker_threads` | |
| `util`, `querystring`, `url`, `assert`, `test`, `zlib` | |

Coverage per module (exact functions and options) is in
[../node-implemented.md](../node-implemented.md); what is missing is in
[../node-unimplemented.md](../node-unimplemented.md).

Remember the runtime model before building on `net`/`http`: the event loop runs
**after** the program body, and `async`/`await` is a synchronous microtask
model. See [language-support.md](./language-support.md).

### `gui` — HTML/CSS window

Enable with `--ext gui` and import the `gui` module:

```ts
import { createWindow, run } from "gui";
```

It is a self-contained GPU-accelerated renderer (own HTML parser, CSS cascade,
layout and compositor) — not a system WebView. It ships as a per-platform
prebuilt `gui.a`/`gui.lib` because it is C++. If that archive is missing for
your platform, the build fails with an actionable message; design and roadmap
live in [../gui.md](../gui.md), and the scripting surface in
[../gui-scripts.md](../gui-scripts.md).

## Third-party npm packages

Bare specifiers that no enabled extension claims are looked up in `node_modules`
and bundled **as source**. A package that is not plain ESM TypeScript/JavaScript
— or that relies on CommonJS, `require`, `__dirname`, or circular dependencies —
will not work. `require()` is rejected with a hint to convert to `import`.

Practical consequence: for a Node program, enabling `--ext node` is usually the
right answer; reach for an npm dependency only when you must.

## Native extensions (C++ / Rust, no compiler changes)

Any code that exposes `extern "C"` entry points with the runtime ABI can be
linked in:

```c
xt_value my_fn(int32_t argc, xt_value *argv);
```

Build it with an **external** toolchain (clang++ or cargo), describe the
artifacts in a JSON manifest, and pass the manifest:

```bash
xbintsc build demo.ts --ext-native ./xbintsc.manifest.json
xbintsc build demo.ts --ext-native a.json,b.json      # several
```

```jsonc
{
  "name": "mathx-cpp",                       // required, unique
  "objects": ["build/mathx.o"],              // .o / .a / .lib, relative to this file
  "linkerFlagsByPlatform": {                 // C++/Rust runtimes
    "linux":  ["-lstdc++", "-lm"],
    "darwin": ["-lc++"],
    "win32":  ["-lmsvcprt"]
  },
  "builtins": { "cppClamp": { "symbol": "mathx_clamp" } },   // no import needed
  "modules": {
    "mathx": { "exports": { "add": { "symbol": "mathx_add" } } }
  }
}
```

`import { add } from "mathx"` then lowers to the native symbol exactly like a C
runtime binding; `builtins` are globally callable without importing.

Authoring helpers: `runtime/xt_ext.h` (C/C++) and `runtime/xt_ext.rs` (Rust) —
they wrap the argument and value helpers. Key ABI facts: values are 64-bit
NaN-boxed words; strings are **not** NUL-terminated, so pair `xt_string_data`
with `xt_string_length_value`; the runtime never moves or frees memory it handed
out. Build with the same clang/ABI that xbintsc resolves (`xbintsc doctor`
prints it), and on Windows build inside an **x64/ARM64 Native Tools Command
Prompt**.

Working projects to copy: [`examples/extensions/cpp`](../../examples/extensions/cpp)
and [`examples/extensions/rust`](../../examples/extensions/rust); the full guide
is [../../examples/extensions/README.md](../../examples/extensions/README.md).

## Programmatic registration

```ts
import { build, createDefaultRegistry, nativeExtensionFromManifest, nodeExtension } from "xbintsc";

const extensions = createDefaultRegistry()
  .register(nodeExtension)
  .register(nativeExtensionFromManifest("./xbintsc.manifest.json"));

build("demo.ts", { extensions });
```

The registry is also how a host tool hints at a disabled extension, which is what
produces the "pass `--ext node`" message instead of a confusing downstream error
([../../src/extensions/catalog.ts](../../src/extensions/catalog.ts)). The list of
bundled extensions is `bundledExtensions()`.
