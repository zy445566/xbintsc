/**
 * Command line interface.
 *
 * Commands are intentionally small and composable so the same operations are
 * available programmatically through the driver API:
 *
 *   xbintsc build <file> [-o out] [--emit ir|obj|exe] [-O0..3] [--ext node]
 *                        [--ext-native manifest.json]
 *   xbintsc run   <file> [-- args...]
 *   xbintsc emit  <file>            # print LLVM IR to stdout
 *   xbintsc doctor                  # report the resolved toolchain
 *   xbintsc version
 */

import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { formatDiagnostic, type Diagnostic } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { build, compileEntry, COMPILER_VERSION, type EmitKind } from "../driver/compiler.js";
import { findRuntimeDir, platformSlug } from "../driver/paths.js";
import { runtimeLibDir } from "../driver/runtime-lib.js";
import { resolveToolchain } from "../driver/toolchain-provider.js";
import { realRunner, ToolchainError } from "../driver/toolchain.js";
import {
  DOC_CLI,
  DOC_TROUBLESHOOTING,
  documentationHints,
  thrownFailure,
  toolchainPointer,
  UNEXPECTED_FAILURE_HINT,
  type DocumentationPointer,
} from "./hints.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";
import { bundledExtensions } from "../extensions/catalog.js";
import { nativeExtensionFromManifest } from "../extensions/native.js";
import { resolveResourceCompiler } from "../driver/win-icon.js";
import {
  CONFIG_FILE_NAME,
  findProjectConfig,
  loadProjectConfig,
  resolveConfigPaths,
  type AppConfig,
  type ProjectConfig,
} from "../driver/config.js";

export interface CliIo {
  readonly stdout: (text: string) => void;
  readonly stderr: (text: string) => void;
}

const defaultIo: CliIo = {
  stdout: (text) => process.stdout.write(text),
  stderr: (text) => process.stderr.write(text),
};

/**
 * `--no-hints` state for the current {@link run} call. A module-level flag (and
 * not a parameter) keeps the hint layer out of the signatures of every helper
 * that can fail.
 */
let hintsDisabled = false;

interface ParsedArgs {
  readonly command?: string;
  readonly positionals: string[];
  readonly flags: Map<string, string | boolean>;
  readonly passthrough: string[];
}

const VALUE_FLAGS = new Set([
  "output",
  "out",
  "emit",
  "optimize",
  "ext",
  "ext-native",
  "config",
  "icon",
  "app-id",
  "app-name",
]);

function parseArgs(argv: readonly string[]): ParsedArgs {
  const positionals: string[] = [];
  const flags = new Map<string, string | boolean>();
  const passthrough: string[] = [];
  let afterSeparator = false;
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index]!;
    if (afterSeparator) {
      passthrough.push(arg);
      continue;
    }
    if (arg === "--") {
      afterSeparator = true;
      continue;
    }
    if (arg.startsWith("--")) {
      const [name, inlineValue] = arg.slice(2).split("=", 2);
      if (inlineValue !== undefined) {
        flags.set(name!, inlineValue);
      } else if (VALUE_FLAGS.has(name!)) {
        const next = argv[index + 1];
        if (next !== undefined && !next.startsWith("-")) {
          flags.set(name!, next);
          index++;
        } else {
          flags.set(name!, true);
        }
      } else {
        flags.set(name!, true);
      }
    } else if (arg.startsWith("-") && arg.length > 1 && !/^-\d/.test(arg)) {
      const name = arg.slice(1);
      if (name.startsWith("O")) {
        flags.set("optimize", name.slice(1) || "2");
      } else if (name === "o") {
        const next = argv[index + 1];
        if (next !== undefined) {
          flags.set("output", next);
          index++;
        }
      } else {
        flags.set(name, true);
      }
    } else {
      positionals.push(arg);
    }
  }
  return { command: positionals.shift(), positionals, flags, passthrough };
}

function buildRegistry(extNames: readonly string[], nativeManifests: readonly string[]): ExtensionRegistry {
  const registry = createDefaultRegistry();
  const bundled = bundledExtensions();
  for (const name of extNames) {
    const extension = bundled.find((candidate) => candidate.name === name);
    if (!extension) throw new Error(`Unknown extension '${name}'`);
    registry.register(extension);
  }
  // Each manifest describes a pre-built C++ or Rust library.
  for (const manifest of nativeManifests) {
    registry.register(nativeExtensionFromManifest(manifest));
  }
  // Hint the extensions that ship with xbintsc but were not enabled, so a
  // missing `import` of a known module points at the flag that enables it
  // (`pass --ext node`) instead of failing later with a confusing error.
  for (const extension of bundled) {
    if (!registry.has(extension.name)) registry.hintExtension(extension);
  }
  return registry;
}

function printDiagnostics(diagnostics: readonly Diagnostic[], fileName: string, io: CliIo): void {
  let source: SourceFile | undefined;
  try {
    source = new SourceFile(fileName, readFileSync(fileName, "utf8"));
  } catch {
    source = undefined;
  }
  for (const diagnostic of diagnostics) {
    // Driver-level diagnostics (icon, config, …) have no source location.
    if (diagnostic.fileName === undefined && diagnostic.start === undefined) {
      io.stderr(`error TS${diagnostic.code}: ${diagnostic.message}\n`);
      continue;
    }
    io.stderr(formatDiagnostic(diagnostic, source) + "\n");
  }
  for (const hint of documentationHints(diagnostics)) emitHint(io, hint);
}

/** Print one `hint:` line unless the caller asked for plain output. */
function emitHint(io: CliIo, line: string): void {
  if (hintsDisabled) return;
  io.stderr(`${line}\n`);
}

/**
 * Report a failure that threw instead of becoming a diagnostic, ending with the
 * documentation pointer that explains it. Without this a toolchain failure is a
 * raw stack trace, which tells a reader nothing about which document answers
 * the question.
 */
function reportThrown(error: unknown, io: CliIo): number {
  const message =
    error instanceof ToolchainError
      ? `xbintsc: ${error.message}`
      : `xbintsc: ${error instanceof Error ? error.message : String(error)}`;
  io.stderr(`${message.trimEnd()}\n`);
  const text = error instanceof ToolchainError ? error.message : message;
  const pointer = error instanceof ToolchainError ? toolchainPointer(text) : thrownPointer(text);
  emitHint(io, `hint: ${pointer.text} — see ${pointer.path}`);
  return 1;
}

/** The document that explains a failure the CLI reports as a plain message. */
function thrownPointer(message: string): DocumentationPointer {
  return thrownFailure(message)?.pointer ?? UNEXPECTED_FAILURE_HINT;
}

/** Report a project-config failure, which never becomes a diagnostic bag. */
function reportConfigError(error: string, io: CliIo): number {
  io.stderr(`xbintsc: ${error}\n`);
  const pointer = /JSON/i.test(error)
    ? `hint: fix the JSON; an unreadable or malformed config fails the build — see ${DOC_TROUBLESHOOTING}`
    : `hint: check the config path and its fields — see ${DOC_CLI}`;
  emitHint(io, pointer);
  return 1;
}

const HELP = `xbintsc ${COMPILER_VERSION} - TypeScript binary compiler

Usage:
  xbintsc build <file.ts> [options]   Compile to a native binary
  xbintsc run <file.ts> [-- args]     Compile and execute
  xbintsc emit <file.ts>              Print LLVM IR
  xbintsc doctor                      Report the resolved toolchain
  xbintsc version                     Print the version
  xbintsc help                        Show this message

Options:
  -o, --output <path>   Output path
      --out <dir>       Output directory (default: build/)
      --emit <kind>     exe | obj | ir (default: exe)
  -O0..-O3              Optimization level (default: -O2)
      --ext <names>     Comma separated extensions (e.g. node)
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
      --no-hints        Do not append the "hint: … see <doc>" line to failures

xbintsc compiles a subset of TypeScript to a native binary. Types are erased
(never checked), Node modules need --ext node, third-party npm imports are not
supported, and clang 16+ is required to link.

Docs: doc/ai/README.md (task guide) - doc/ai/language-support.md (subset) -
doc/ai/troubleshooting.md (failures) - llms.txt (index of every document)
`;

function firstLine(text: string): string {
  const index = text.indexOf("\n");
  return (index === -1 ? text : text.slice(0, index)).trim();
}

/** Report the resolved toolchain and runtime locations. */
function doctor(io: CliIo): number {
  io.stdout(`xbintsc ${COMPILER_VERSION}\n`);
  io.stdout(`platform   : ${platformSlug()}\n`);

  try {
    const toolchain = resolveToolchain();
    const version = realRunner.run(toolchain.clang, ["--version"], { env: toolchain.env }).stdout;
    io.stdout(`toolchain  : ${toolchain.clang} (${toolchain.source})\n`);
    io.stdout(`compiler   : ${firstLine(version) || "unknown"}\n`);
  } catch {
    io.stdout("toolchain  : not found\n");
    io.stdout("  install one of: Xcode Command Line Tools (`xcode-select --install`) on macOS,\n");
    io.stdout("  clang+lld (apt/dnf/pacman) on Linux, or LLVM + Visual Studio C++ build tools on Windows,\n");
    io.stdout("  then re-run; or set xbintsc_CLANG to a clang binary. See doc/requirements.md.\n");
  }

  try {
    io.stdout(`runtime    : ${findRuntimeDir()}\n`);
  } catch {
    io.stdout("runtime    : not found (cannot locate runtime/rt.h)\n");
  }

  try {
    io.stdout(`runtime lib: ${runtimeLibDir()}\n`);
  } catch {
    io.stdout("runtime lib: unavailable\n");
  }

  try {
    const toolchain = resolveToolchain();
    const resource = resolveResourceCompiler(realRunner, toolchain.clang);
    io.stdout(`icon res   : ${resource ? `${resource.tool} (${resource.kind})` : "none (.exe icon disabled)"}\n`);
  } catch {
    io.stdout("icon res   : unknown\n");
  }
  return 0;
}

type ConfigResolution =
  | { readonly ok: true; readonly config: ProjectConfig }
  | { readonly ok: false; readonly error: string };

/**
 * Resolve the project config for a command: `--config <path>` selects one
 * explicitly, `--no-config` disables discovery, otherwise we walk up from the
 * entry file (or the cwd) looking for {@link CONFIG_FILE_NAME}.
 */
function resolveProjectConfig(
  flags: Map<string, string | boolean>,
  cliEntry: string | undefined,
): ConfigResolution {
  try {
    const explicit = flags.get("config");
    if (typeof explicit === "string") {
      return { ok: true, config: resolveConfigPaths(loadProjectConfig(explicit)) };
    }
    if (flags.has("no-config")) return { ok: true, config: {} };
    const startDir = cliEntry ? dirname(resolve(cliEntry)) : process.cwd();
    const found = findProjectConfig(startDir);
    if (!found) return { ok: true, config: {} };
    return { ok: true, config: resolveConfigPaths(loadProjectConfig(found)) };
  } catch (error) {
    return { ok: false, error: (error as Error).message };
  }
}

/** Merge `app.*` config with the CLI overrides (`--icon`, `--bundle`, …). */
function mergeAppConfig(flags: Map<string, string | boolean>, config: ProjectConfig): AppConfig {
  const base = config.app ?? {};
  const name = typeof flags.get("app-name") === "string" ? (flags.get("app-name") as string) : base.name;
  const bundleId = typeof flags.get("app-id") === "string" ? (flags.get("app-id") as string) : base.bundleId;
  const bundle = flags.has("bundle") ? true : base.bundle;
  const icon = typeof flags.get("icon") === "string" ? (flags.get("icon") as string) : base.icon;
  return {
    ...(name !== undefined ? { name } : {}),
    ...(bundleId !== undefined ? { bundleId } : {}),
    ...(bundle !== undefined ? { bundle } : {}),
    ...(icon !== undefined ? { icon } : {}),
  };
}

/** Split a comma-separated CLI flag into a trimmed, non-empty list. */
function listFlag(flags: Map<string, string | boolean>, name: string): string[] | undefined {
  const value = flags.get(name);
  if (typeof value !== "string") return undefined;
  return value
    .split(",")
    .map((entry) => entry.trim())
    .filter(Boolean);
}

export function run(argv: readonly string[], io: CliIo = defaultIo): number {
  const disableHints = parseArgs(argv).flags.has("no-hints");
  const previous = hintsDisabled;
  hintsDisabled = disableHints;
  try {
    return dispatch(argv, io);
  } catch (error) {
    return reportThrown(error, io);
  } finally {
    hintsDisabled = previous;
  }
}

function dispatch(argv: readonly string[], io: CliIo): number {
  const args = parseArgs(argv);
  const command = args.command;

  if (!command || command === "help" || args.flags.has("help")) {
    io.stdout(HELP);
    return 0;
  }

  if (command === "doctor") {
    return doctor(io);
  }

  if (command === "version" || args.flags.has("version")) {
    io.stdout(`xbintsc ${COMPILER_VERSION}\n`);
    return 0;
  }

  if (command === "emit") {
    const cliEntry = args.positionals[0];
    const resolved = resolveProjectConfig(args.flags, cliEntry);
    if (!resolved.ok) return reportConfigError(resolved.error, io);
    const entry = cliEntry ?? resolved.config.entry;
    if (!entry) {
      io.stderr(`xbintsc: emit requires a source file (or 'entry' in ${CONFIG_FILE_NAME})\n`);
      emitHint(io, `hint: pass the entry file, or set 'entry' in ${CONFIG_FILE_NAME} — see ${DOC_CLI}`);
      return 1;
    }
    const extNames = listFlag(args.flags, "ext") ?? resolved.config.extensions ?? [];
    const nativeManifests = listFlag(args.flags, "ext-native") ?? resolved.config.extNative ?? [];
    const registry = buildRegistry(extNames, nativeManifests);
    const { ir, diagnostics } = compileEntry(entry, registry);
    if (diagnostics.some((d) => d.category === "error")) {
      printDiagnostics(diagnostics, entry, io);
      return 1;
    }
    io.stdout(ir);
    return 0;
  }

  if (command === "build" || command === "run") {
    const cliEntry = args.positionals[0];
    const resolved = resolveProjectConfig(args.flags, cliEntry);
    if (!resolved.ok) return reportConfigError(resolved.error, io);
    const config = resolved.config;
    const entry = cliEntry ?? config.entry;
    if (!entry) {
      io.stderr(`xbintsc: ${command} requires a source file (or 'entry' in ${CONFIG_FILE_NAME})\n`);
      emitHint(io, `hint: pass the entry file, or set 'entry' in ${CONFIG_FILE_NAME} — see ${DOC_CLI}`);
      return 1;
    }
    const emit = (args.flags.get("emit") as EmitKind | undefined) ?? "exe";
    const optimize = (args.flags.get("optimize") as string | undefined) ?? config.optimize ?? "2";
    const extNames = listFlag(args.flags, "ext") ?? config.extensions ?? [];
    const nativeManifests = listFlag(args.flags, "ext-native") ?? config.extNative ?? [];
    const registry = buildRegistry(extNames, nativeManifests);

    const result = build(entry, {
      output: typeof args.flags.get("output") === "string" ? (args.flags.get("output") as string) : config.output,
      outDir: typeof args.flags.get("out") === "string" ? (args.flags.get("out") as string) : config.outDir,
      emit,
      optimize: optimize as "0" | "1" | "2" | "3",
      force: Boolean(args.flags.get("force")) || config.force === true,
      verbose: Boolean(args.flags.get("verbose")),
      extensions: registry,
      app: mergeAppConfig(args.flags, config),
    });

    if (result.diagnostics.some((d) => d.category === "error")) {
      printDiagnostics(result.diagnostics, entry, io);
      return 1;
    }

    if (command === "build") {
      const bundle = result.bundlePath ? ` (bundle: ${result.bundlePath})` : "";
      io.stdout(`xbintsc: wrote ${result.outputPath}${result.cached ? " (cached)" : ""}${bundle}\n`);
      return 0;
    }

    if (emit !== "exe") {
      io.stderr("xbintsc: run requires --emit exe\n");
      return 1;
    }
    const executed = spawnSync(result.outputPath, args.passthrough, { stdio: "inherit" });
    return executed.status ?? 0;
  }

  io.stderr(`xbintsc: unknown command '${command}'\n\n${HELP}`);
  return 1;
}

const isMain =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) {
  process.exit(run(process.argv.slice(2)));
}
