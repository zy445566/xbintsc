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
import { build, compileEntry, COMPILER_VERSION, type EmitKind, type PhaseTiming } from "../driver/compiler.js";
import { findRuntimeDir, platformSlug } from "../driver/paths.js";
import { findRuntimeLibrary, runtimeLibDir } from "../driver/runtime-lib.js";
import { resolveToolchain, type ResolvedToolchain } from "../driver/toolchain-provider.js";
import { MINIMUM_CLANG_MAJOR, probeClang, realRunner, ToolchainError } from "../driver/toolchain.js";
import {
  DOC_CLI,
  DOC_TROUBLESHOOTING,
  documentationHints,
  thrownFailure,
  toolchainPointer,
  UNEXPECTED_FAILURE_HINT,
} from "./hints.js";
import { buildRegistry, normalizeExtensions } from "./extensions.js";
import { resolveResourceCompiler } from "../driver/win-icon.js";
import {
  CONFIG_FILE_NAME,
  findProjectConfig,
  loadProjectConfig,
  resolveConfigPaths,
  type AppConfig,
  type ProjectConfig,
} from "../driver/config.js";
import { fingerprintTree, startWatching } from "./watch.js";
import { HELP } from "./help.js";

/** Sentinel: watch mode keeps the process alive instead of exiting. */
const KEEP_RUNNING = -1;

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
  "feature",
  "no-feature",
  "config",
  "icon",
  "app-id",
  "app-name",
  "pgo-generate",
  "pgo-use",
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

function printDiagnostics(diagnostics: readonly Diagnostic[], fileName: string, io: CliIo): void {
  /* A diagnostic can come from any module in a bundled program, so load (and
   * cache) the source of the file it names instead of always using the entry:
   * reusing the entry's text is what used to print another file's line with a
   * caret under unrelated code. */
  const sources = new Map<string, SourceFile | undefined>();
  const sourceFor = (name: string): SourceFile | undefined => {
    if (!sources.has(name)) {
      try {
        sources.set(name, new SourceFile(name, readFileSync(name, "utf8")));
      } catch {
        sources.set(name, undefined);
      }
    }
    return sources.get(name);
  };
  for (const diagnostic of diagnostics) {
    // Driver-level diagnostics (icon, config, …) have no source location.
    if (diagnostic.fileName === undefined && diagnostic.start === undefined) {
      io.stderr(`error TS${diagnostic.code}: ${diagnostic.message}\n`);
      continue;
    }
    const source = sourceFor(diagnostic.fileName ?? fileName);
    io.stderr(formatDiagnostic(diagnostic, source) + "\n");
  }
  for (const hint of documentationHints(diagnostics)) emitHint(io, hint);
}

/** Print the phases collected by `--time`. */
function printTimings(timings: readonly PhaseTiming[], io: CliIo): void {
  const text = timings.map((timing) => `${timing.phase} ${timing.ms}ms`).join(", ");
  io.stdout(`xbintsc: timing (${text})\n`);
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
  if (error instanceof ToolchainError) {
    io.stderr(`xbintsc: ${error.message.trimEnd()}\n`);
    const pointer = toolchainPointer(error.message);
    emitHint(io, `hint: ${pointer.text} — see ${pointer.path}`);
    return 1;
  }
  const message = error instanceof Error ? error.message : String(error);
  const failure = thrownFailure(message);
  if (failure === undefined) {
    /* An uncaught exception that is not a known toolchain/setup failure is a
     * compiler bug: say so instead of blaming the user's source or toolchain. */
    io.stderr(`xbintsc: internal error: ${message.trimEnd()}\n`);
    emitHint(
      io,
      `hint: this is a bug in xbintsc, not in your program — please report it with the input that triggered it — see ${UNEXPECTED_FAILURE_HINT.path}`,
    );
    return 1;
  }
  io.stderr(`xbintsc: ${message.trimEnd()}\n`);
  emitHint(io, `hint: ${failure.pointer.text} — see ${failure.pointer.path}`);
  return 1;
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

/**
 * Report the resolved toolchain and runtime locations.
 *
 * Returns non-zero when the resolved compiler cannot build anything (missing,
 * or older than {@link MINIMUM_CLANG_MAJOR}), so CI and scripts can use it as a
 * preflight instead of discovering the problem halfway through a build.
 */
function doctor(io: CliIo): number {
  io.stdout(`xbintsc ${COMPILER_VERSION}\n`);
  io.stdout(`platform   : ${platformSlug()}\n`);

  let toolchain: ResolvedToolchain | undefined;
  let usable = false;
  try {
    toolchain = resolveToolchain();
    const probe = probeClang(realRunner, toolchain.clang, toolchain.env);
    io.stdout(`toolchain  : ${toolchain.clang} (${toolchain.source})\n`);
    io.stdout(`compiler   : ${probe.summary || "unknown"}\n`);
    const version = toolchain.version ?? probe.version;
    if (!version) {
      // An explicit wrapper need not print a clang-style version; let it run.
      usable = true;
      io.stdout(`status     : version unknown (requires clang >= ${MINIMUM_CLANG_MAJOR})\n`);
    } else if (version.major >= MINIMUM_CLANG_MAJOR) {
      usable = true;
      io.stdout(`status     : ok (requires clang >= ${MINIMUM_CLANG_MAJOR})\n`);
    } else {
      io.stdout(
        `status     : too old: clang ${version.major}.${version.minor}.${version.patch} ` +
          `(requires >= ${MINIMUM_CLANG_MAJOR})\n`,
      );
      io.stdout("  set xbintsc_CLANG to a newer clang; on Windows Visual Studio ships one under\n");
      io.stdout("  <VS install>\\VC\\Tools\\Llvm\\x64\\bin\\clang.exe. See doc/requirements.md.\n");
    }
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    io.stdout("toolchain  : not usable\n");
    for (const line of message.split("\n")) {
      if (line.trim().length > 0) io.stdout(`  ${line.trim()}\n`);
    }
    io.stdout("  See doc/requirements.md for the per-platform install steps, or set xbintsc_CLANG.\n");
  }

  try {
    io.stdout(`runtime    : ${findRuntimeDir()}\n`);
  } catch {
    io.stdout("runtime    : not found (cannot locate runtime/rt.h)\n");
  }

  try {
    io.stdout(`runtime lib: ${runtimeLibDir()}\n`);
    const archives = ["core", "ext_node", "gui", "media"].filter((name) => findRuntimeLibrary(name) !== undefined);
    io.stdout(
      `archives   : ${archives.length > 0 ? archives.join(", ") : "none (C runtime compiled on demand)"}\n`,
    );
  } catch {
    io.stdout("runtime lib: unavailable\n");
  }

  try {
    if (!toolchain) throw new Error("no toolchain");
    const resource = resolveResourceCompiler(realRunner, toolchain.clang);
    io.stdout(`icon res   : ${resource ? resource.tool : "none (.exe icon disabled)"}\n`);
  } catch {
    io.stdout("icon res   : unknown\n");
  }
  return usable ? 0 : 1;
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

  if (command === "check") {
    const cliEntry = args.positionals[0];
    const resolved = resolveProjectConfig(args.flags, cliEntry);
    if (!resolved.ok) return reportConfigError(resolved.error, io);
    const entry = cliEntry ?? resolved.config.entry;
    if (!entry) {
      io.stderr(`xbintsc: check requires a source file (or 'entry' in ${CONFIG_FILE_NAME})\n`);
      emitHint(io, `hint: pass the entry file, or set 'entry' in ${CONFIG_FILE_NAME} — see ${DOC_CLI}`);
      return 1;
    }
    const extNames = listFlag(args.flags, "ext") ?? resolved.config.extensions ?? [];
    const nativeManifests = listFlag(args.flags, "ext-native") ?? resolved.config.extNative ?? [];
    const { extensions: registryExtensions } = normalizeExtensions(extNames, io);
    const registry = buildRegistry(registryExtensions, nativeManifests);
    /* Front-end only: parse, bundle, check and lower far enough to report
     * semantic / unsupported-feature diagnostics, but never invoke clang or
     * write output. The checker is on unless `--no-check` is passed. */
    const { diagnostics } = compileEntry(entry, registry, {
      check: !args.flags.has("no-check"),
    });
    const failed = diagnostics.some((diagnostic) => diagnostic.category === "error");
    if (args.flags.has("json")) {
      io.stdout(`${JSON.stringify(diagnostics)}\n`);
    } else if (failed) {
      printDiagnostics(diagnostics, entry, io);
    }
    return failed ? 1 : 0;
  }

  if (command === "watch") {
    const cliEntry = args.positionals[0];
    const resolved = resolveProjectConfig(args.flags, cliEntry);
    if (!resolved.ok) return reportConfigError(resolved.error, io);
    const entry = cliEntry ?? resolved.config.entry;
    if (!entry) {
      io.stderr(`xbintsc: watch requires a source file (or 'entry' in ${CONFIG_FILE_NAME})\n`);
      emitHint(io, `hint: pass the entry file, or set 'entry' in ${CONFIG_FILE_NAME} — see ${DOC_CLI}`);
      return 1;
    }
    /* Re-enter the normal build path with the same flags, exactly as if the
     * user had typed `build`; the config is re-read on every rebuild too. */
    const forwarded = ["build", ...argv.slice(1)];
    run(forwarded, io);
    const root = dirname(resolve(entry));
    startWatching({
      scan: () => fingerprintTree(root),
      onRebuild: () => {
        io.stdout("\n");
        run(forwarded, io);
      },
    });
    io.stdout(`xbintsc: watching ${root} for changes (Ctrl+C to stop)\n`);
    return KEEP_RUNNING;
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
    const { extensions: registryExtensions } = normalizeExtensions(extNames, io);
    const registry = buildRegistry(registryExtensions, nativeManifests);
    const { ir, diagnostics } = compileEntry(entry, registry, {
      debug: Boolean(args.flags.get("debug")),
      check: !args.flags.has("no-check"),
    });
    if (diagnostics.some((d) => d.category === "error")) {
      if (args.flags.has("json")) {
        io.stdout(`${JSON.stringify(diagnostics)}\n`);
        return 1;
      }
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
    const normalized = normalizeExtensions(extNames, io);
    const registry = buildRegistry(normalized.extensions, nativeManifests);
    const features = [
      ...normalized.features,
      ...(listFlag(args.flags, "feature") ?? config.features ?? []),
    ];
    const noFeatures = listFlag(args.flags, "no-feature") ?? config.noFeatures ?? [];
    const pgoGenerate = args.flags.get("pgo-generate");
    const pgoUse = args.flags.get("pgo-use");
    if (pgoGenerate !== undefined && pgoUse !== undefined) {
      io.stderr("xbintsc: --pgo-generate and --pgo-use are mutually exclusive\n");
      return 1;
    }
    const pgo =
      pgoGenerate !== undefined
        ? ("generate" as const)
        : pgoUse !== undefined
          ? ("use" as const)
          : config.pgo;
    const pgoDir =
      typeof pgoGenerate === "string"
        ? pgoGenerate
        : typeof pgoUse === "string"
          ? pgoUse
          : config.pgoDir;

    const result = build(entry, {
      output: typeof args.flags.get("output") === "string" ? (args.flags.get("output") as string) : config.output,
      outDir: typeof args.flags.get("out") === "string" ? (args.flags.get("out") as string) : config.outDir,
      emit,
      optimize: optimize as "0" | "1" | "2" | "3",
      force: Boolean(args.flags.get("force")) || config.force === true,
      verbose: Boolean(args.flags.get("verbose")),
      extensions: registry,
      app: mergeAppConfig(args.flags, config),
      debug: Boolean(args.flags.get("debug")),
      check: !args.flags.has("no-check"),
      lto: Boolean(args.flags.get("lto")) || config.lto === true,
      ...(pgo ? { pgo } : {}),
      ...(pgoDir ? { pgoDir } : {}),
      ...(features.length > 0 ? { features } : {}),
      ...(noFeatures.length > 0 ? { noFeatures } : {}),
    });

    if (result.diagnostics.some((d) => d.category === "error")) {
      if (args.flags.has("json")) {
        io.stdout(`${JSON.stringify(result.diagnostics)}\n`);
        return 1;
      }
      printDiagnostics(result.diagnostics, entry, io);
      return 1;
    }

    /* A detected-but-unavailable feature is a warning, not a failure: the
     * program still links and the capability degrades at run time. */
    const warnings = result.diagnostics.filter((d) => d.category === "warning");
    if (warnings.length > 0) printDiagnostics(warnings, entry, io);

    if (args.flags.has("time") && result.timings) printTimings(result.timings, io);

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
  const status = run(process.argv.slice(2));
  /* Watch mode returns KEEP_RUNNING; the polling interval keeps the process
   * alive, so calling process.exit here would stop it immediately. */
  if (status !== KEEP_RUNNING) process.exit(status);
}
