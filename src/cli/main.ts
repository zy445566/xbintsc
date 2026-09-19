/**
 * Command line interface.
 *
 * Commands are intentionally small and composable so the same operations are
 * available programmatically through the driver API:
 *
 *   xbtsc build <file> [-o out] [--emit ir|obj|exe] [-O0..3] [--ext node]
 *   xbtsc run   <file> [-- args...]
 *   xbtsc emit  <file>            # print LLVM IR to stdout
 *   xbtsc version
 */

import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { DiagnosticBag, formatDiagnostic, type Diagnostic } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { build, compileString, COMPILER_VERSION, type EmitKind } from "../driver/compiler.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";
import { nodeExtension } from "../extensions/node.js";

export interface CliIo {
  readonly stdout: (text: string) => void;
  readonly stderr: (text: string) => void;
}

const defaultIo: CliIo = {
  stdout: (text) => process.stdout.write(text),
  stderr: (text) => process.stderr.write(text),
};

interface ParsedArgs {
  readonly command?: string;
  readonly positionals: string[];
  readonly flags: Map<string, string | boolean>;
  readonly passthrough: string[];
}

const VALUE_FLAGS = new Set(["output", "out", "emit", "optimize", "ext"]);

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

function buildRegistry(flags: Map<string, string | boolean>): ExtensionRegistry {
  const registry = createDefaultRegistry();
  const requested = flags.get("ext");
  if (typeof requested === "string") {
    for (const name of requested.split(",").map((n) => n.trim()).filter(Boolean)) {
      if (name === "node") registry.register(nodeExtension);
      else throw new Error(`Unknown extension '${name}'`);
    }
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
    io.stderr(formatDiagnostic(diagnostic, source) + "\n");
  }
}

const HELP = `xbtsc ${COMPILER_VERSION} - TypeScript binary compiler

Usage:
  xbtsc build <file.ts> [options]   Compile to a native binary
  xbtsc run <file.ts> [-- args]     Compile and execute
  xbtsc emit <file.ts>              Print LLVM IR
  xbtsc version                     Print the version
  xbtsc help                        Show this message

Options:
  -o, --output <path>   Output path
      --out <dir>       Output directory (default: build/)
      --emit <kind>     exe | obj | ir (default: exe)
  -O0..-O3              Optimization level (default: -O2)
      --ext <names>     Comma separated extensions (e.g. node)
      --force           Ignore the incremental cache
      --verbose         Print progress information
`;

export function run(argv: readonly string[], io: CliIo = defaultIo): number {
  const args = parseArgs(argv);
  const command = args.command;

  if (!command || command === "help" || args.flags.has("help")) {
    io.stdout(HELP);
    return 0;
  }

  if (command === "version" || args.flags.has("version")) {
    io.stdout(`xbtsc ${COMPILER_VERSION}\n`);
    return 0;
  }

  if (command === "emit") {
    const entry = args.positionals[0];
    if (!entry) {
      io.stderr("xbtsc: emit requires a source file\n");
      return 1;
    }
    const source = readFileSync(resolve(entry), "utf8");
    const registry = buildRegistry(args.flags);
    const { ir, diagnostics } = compileString(source, entry, registry);
    if (diagnostics.some((d) => d.category === "error")) {
      printDiagnostics(diagnostics, entry, io);
      return 1;
    }
    io.stdout(ir);
    return 0;
  }

  if (command === "build" || command === "run") {
    const entry = args.positionals[0];
    if (!entry) {
      io.stderr(`xbtsc: ${command} requires a source file\n`);
      return 1;
    }
    const emit = (args.flags.get("emit") as EmitKind | undefined) ?? "exe";
    const optimize = (args.flags.get("optimize") as string | undefined) ?? "2";
    const result = build(entry, {
      output: typeof args.flags.get("output") === "string" ? (args.flags.get("output") as string) : undefined,
      outDir: typeof args.flags.get("out") === "string" ? (args.flags.get("out") as string) : undefined,
      emit,
      optimize: optimize as "0" | "1" | "2" | "3",
      force: Boolean(args.flags.get("force")),
      verbose: Boolean(args.flags.get("verbose")),
      extensions: buildRegistry(args.flags),
    });

    if (result.diagnostics.some((d) => d.category === "error")) {
      printDiagnostics(result.diagnostics, entry, io);
      return 1;
    }

    if (command === "build") {
      io.stdout(`xbtsc: wrote ${result.outputPath}${result.cached ? " (cached)" : ""}\n`);
      return 0;
    }

    if (emit !== "exe") {
      io.stderr("xbtsc: run requires --emit exe\n");
      return 1;
    }
    const executed = spawnSync(result.outputPath, args.passthrough, { stdio: "inherit" });
    return executed.status ?? 0;
  }

  io.stderr(`xbtsc: unknown command '${command}'\n\n${HELP}`);
  return 1;
}

const isMain =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isMain) {
  process.exit(run(process.argv.slice(2)));
}
