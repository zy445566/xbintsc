# Native extensions (C++ / Rust)

xbintsc can link code that was compiled **outside** the TypeScript pipeline, as
long as it exposes `extern "C"` entry points with the runtime ABI:

```c
xt_value my_fn(int32_t argc, xt_value *argv);
```

The intended workflow is exactly that of a normal native build:

```
mathx.cpp / lib.rs
   │  clang++ -c  /  cargo build          (an external build)
   ▼
libmathx.a            extern "C" symbols + xt ABI
   │  xbintsc build demo.ts --ext-native xbintsc.manifest.json
   ▼
demo                  one standalone executable
```

A small JSON **manifest** tells xbintsc where the pre-built artifacts are and
how their symbols map onto global functions and importable modules. The core
compiler never learns that the extension was written in C++ or Rust — it links
the objects verbatim and wires the bindings like any other extension.

## Manifest format

```jsonc
{
  "name": "mathx-cpp",                      // required, unique
  "description": "…",                       // optional
  "objects": ["build/libmathx.a"],          // .o / .a / .lib, relative to this file
  "linkerFlags": ["-lm"],                   // flags for every platform
  "linkerFlagsByPlatform": {                // per-OS flags (C++/Rust runtimes)
    "linux":  ["-lstdc++"],
    "darwin": ["-lc++"],
    "win32":  ["-lc++", "-static"]
  },
  "builtins": {                             // globals callable without import
    "cppClamp": { "symbol": "mathx_clamp" }
  },
  "modules": {                              // importable specifiers
    "mathx": {
      "exports": {
        "add":  { "symbol": "mathx_add" },
        "fib":  { "symbol": "mathx_fib" }
        // "path": { "namespace": "path", "method": "join" }  // namespace dispatch
      }
    }
  }
}
```

Use `--ext-native <manifest>` (comma separated for several) to register one
or more manifests:

```bash
xbintsc build demo.ts --ext-native path/to/xbintsc.manifest.json
# several at once
xbintsc build demo.ts --ext-native a.json,b.json
```

## Authoring the native side

Include `runtime/xt_ext.h` (C/C++) or `runtime/xt_ext.rs` (Rust) for the ABI
plus small argument/value helpers. Compile with the same clang xbintsc uses
(`xbintsc doctor` prints the runtime directory).

### C++

```cpp
#include "xt_ext.h"

XT_EXT_FN(mathx_add) {
  double a = xt_ext_number(argc, argv, 0, 0);
  double b = xt_ext_number(argc, argv, 1, 0);
  return xt_number(a + b);
}
```

```bash
clang++ -O2 -fPIC -I<runtime> -c mathx.cpp -o mathx.o
ar rcs libmathx.a mathx.o
```

### Rust

```rust
include!("runtime/xt_ext.rs");

#[no_mangle]
pub extern "C" fn mathx_add(argc: i32, argv: *const XtValue) -> XtValue {
    let a = arg_number(argc, argv, 0, 0.0);
    let b = arg_number(argc, argv, 1, 0.0);
    xt_number(a + b)
}
```

```toml
# Cargo.toml
[lib]
crate-type = ["staticlib"]
```

```bash
cargo build --release    # -> target/release/libmathx.a
# Windows (MinGW): build the gnullvm target instead
cargo build --release --target x86_64-pc-windows-gnullvm
```

## The examples in this directory

| Directory | Language | Build | Manifest |
| --- | --- | --- | --- |
| [`cpp/`](./cpp) | C++17 (`std::string`, templates) | `cpp/build.sh` | `cpp/xbintsc.manifest.json` |
| [`rust/`](./rust) | Rust (`std`, `Vec`, `String`) | `rust/build.sh` | `rust/xbintsc.manifest.json` |

Both expose the same module (`mathx`) and a global clamp helper
(`cppClamp` / `rustClamp`):

```bash
# C++
./examples/extensions/cpp/build.sh
npx tsx src/cli/main.ts run examples/extensions/cpp/demo.ts \
  --ext-native examples/extensions/cpp/xbintsc.manifest.json

# Rust
./examples/extensions/rust/build.sh
npx tsx src/cli/main.ts run examples/extensions/rust/demo.ts \
  --ext-native examples/extensions/rust/xbintsc.manifest.json
```

Programmatic use:

```ts
import { build, createDefaultRegistry, nativeExtensionFromManifest } from "xbintsc";

const extensions = createDefaultRegistry().register(
  nativeExtensionFromManifest("examples/extensions/cpp/xbintsc.manifest.json"),
);
build("demo.ts", { extensions });
```

## ABI notes

- Values are 64-bit NaN-boxed words (`uint64_t`). Doubles are unboxed; build
  results with `xt_number`, `xt_string_new`, `xt_array_new`, `xt_object_new`, …
- Constructors and coercions are declared in `runtime/rt.h`; helpers for reading
  arguments are in `runtime/xt_ext.h` / `runtime/xt_ext.rs`.
- Strings are not NUL-terminated in general; always pair `xt_string_data` with
  `xt_string_length_value`.
- The runtime arena never frees, so memory returned to TypeScript is owned by
  the runtime and never moves.

## Platform notes

All three platforms are supported on the bundled toolchains:

| OS | Compiler xbintsc links with | C++ runtime flag | Rust target | Rust extra flag |
| --- | --- | --- | --- | --- |
| Linux | bundled LLVM | `-lstdc++` | host | `-lpthread -ldl -lm` |
| macOS | Xcode Command Line Tools | `-lc++` | host | `-liconv -framework Security` |
| Windows | bundled MinGW-w64 (llvm-mingw) | `-lc++ -static` | `*-pc-windows-gnullvm` | `-lntdll -static` |

The mechanism is ABI-agnostic — it links whatever objects/archives you give it.
On Windows you must build against the **same** toolchain xbintsc links with:

- **C++**: use the bundled `clang++`/`llvm-ar` (MinGW-w64, UCRT, libc++), as the
  `compile-examples` CI job does. An MSVC-built `.obj`/`.lib` is a different ABI
  and will not link; if you prefer MSVC, ship the artifacts it produces and list
  its C++ runtime in `linkerFlagsByPlatform.win32` instead.
- **Rust**: build the `x86_64-pc-windows-gnullvm` (x64) or
  `aarch64-pc-windows-gnullvm` (arm64) target — *not* the default
  `*-pc-windows-msvc` — so the archive is MinGW-compatible, and add `-lntdll`
  for Rust's standard library.
- **`-static`** keeps the produced executable free of the toolchain's runtime
  DLLs (`libc++.dll`, `libunwind.dll`): without it the link prefers the shared
  import libraries and the binary fails to start off the build machine.

The `compile-examples` CI job (`.github/workflows/ci.yml`) builds both language
examples on every OS in the matrix, Windows included.
