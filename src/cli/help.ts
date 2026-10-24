/**
 * The `--help` text.
 *
 * It lives apart from the command dispatch so the executable entry point stays
 * under the repository's per-file line budget; the text is data, not logic.
 */

import { CONFIG_FILE_NAME } from "../driver/config.js";
import { COMPILER_VERSION } from "../driver/version.js";

export const HELP = `xbintsc ${COMPILER_VERSION} - TypeScript binary compiler

Usage:
  xbintsc build <file.ts> [options]   Compile to a native binary
  xbintsc run <file.ts> [-- args]     Compile and execute
  xbintsc check <file.ts>             Parse, bind and check without linking
  xbintsc watch <file.ts> [options]   Rebuild whenever the source tree changes
  xbintsc emit <file.ts>              Print LLVM IR
  xbintsc doctor                      Report the resolved toolchain
  xbintsc version                     Print the version
  xbintsc help                        Show this message

Options:
  -o, --output <path>   Output path
      --out <dir>       Output directory (default: build/)
      --emit <kind>     exe | obj | ir (default: exe)
  -O0..-O3              Optimization level (default: -O2)
      --ext <names>     Comma separated extensions (e.g. node,gui,media)
      --ext-native <m>  Register a C++/Rust extension from a JSON manifest
                        (comma separated for several)
      --config <path>   Use a project config (default: auto-detect
                        ${CONFIG_FILE_NAME})
      --no-config       Do not read any project config
      --icon <path>     Embed an application icon (PNG/ICO/ICNS)
      --bundle          macOS: also produce a .app bundle
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
      --no-hints        Do not append the "hint: … see <doc>" line to failures

xbintsc compiles a subset of TypeScript to a native binary. Types are erased;
only inference-free facts (names, redeclarations, call arity) are checked.
Node built-ins need --ext node, packages in node_modules are bundled at build
time, and clang 16+ is required to link.

Docs: doc/ai/README.md (task guide) - doc/ai/language-support.md (subset) -
doc/ai/troubleshooting.md (failures) - llms.txt (index of every document)
`;
