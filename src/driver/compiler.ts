/**
 * The build driver: source file in, native binary out.
 *
 * Pipeline: read -> parse -> bind/check -> LLVM IR -> object -> link.
 * The runtime (and any registered extension sources) are compiled once and
 * cached, and the whole build is skipped when the incremental cache says
 * nothing relevant changed.
 */

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, extname, join, resolve } from "node:path";
import { DiagnosticBag, type Diagnostic } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { Parser } from "../parser/parser.js";
import { generate } from "../codegen/llvm.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";
import { BuildCache, hashParts } from "./cache.js";
import { findRuntimeDir } from "./paths.js";
import {
  compileC,
  compileIr,
  findClang,
  link,
  realRunner,
  type Runner,
} from "./toolchain.js";

export const COMPILER_VERSION = "0.1.0";

export type EmitKind = "exe" | "obj" | "ir";

export interface BuildOptions {
  /** Explicit output path (defaults to the source base name in `outDir`). */
  readonly output?: string;
  readonly outDir?: string;
  readonly optimize?: "0" | "1" | "2" | "3";
  readonly emit?: EmitKind;
  readonly force?: boolean;
  readonly verbose?: boolean;
  readonly cacheDir?: string;
  readonly extensions?: ExtensionRegistry;
  readonly runner?: Runner;
  readonly clang?: string;
  readonly write?: boolean;
}

export interface BuildResult {
  readonly outputPath: string;
  readonly irPath?: string;
  readonly cached: boolean;
  readonly diagnostics: readonly Diagnostic[];
  readonly ir?: string;
}

export interface CompileStringResult {
  readonly ir: string;
  readonly diagnostics: readonly Diagnostic[];
}

/** Compile TypeScript source text to LLVM IR without touching the toolchain. */
export function compileString(source: string, fileName = "input.ts", extensions?: ExtensionRegistry): CompileStringResult {
  const file = new SourceFile(fileName, source);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(file, diagnostics);
  const sourceFile = parser.parseSourceFile();
  const registry = extensions ?? createDefaultRegistry();
  const { ir } = generate(sourceFile, diagnostics, { builtins: registry.builtins() });
  return { ir, diagnostics: diagnostics.diagnostics };
}

function executableName(entry: string, outDir: string, explicit: string | undefined): string {
  if (explicit) return resolve(explicit);
  const base = basename(entry, extname(entry));
  const suffix = process.platform === "win32" ? ".exe" : "";
  return resolve(outDir, base + suffix);
}

export function build(entryPath: string, options: BuildOptions = {}): BuildResult {
  const runner = options.runner ?? realRunner;
  const registry = options.extensions ?? createDefaultRegistry();
  const outDir = resolve(options.outDir ?? join(process.cwd(), "build"));
  const cacheDir = resolve(options.cacheDir ?? join(process.cwd(), ".xbintsc"));
  const emit: EmitKind = options.emit ?? "exe";
  const optimize = options.optimize ?? "2";

  const absoluteEntry = resolve(entryPath);
  const sourceText = readFileSync(absoluteEntry, "utf8");

  const baseName = basename(absoluteEntry, extname(absoluteEntry));
  const outputPath =
    emit === "ir"
      ? resolve(options.output ?? join(outDir, baseName + ".ll"))
      : emit === "obj"
        ? resolve(options.output ?? join(outDir, baseName + ".o"))
        : executableName(absoluteEntry, outDir, options.output);

  const cacheKey = hashParts([
    COMPILER_VERSION,
    sourceText,
    emit,
    optimize,
    process.platform,
    registry.all().map((e) => e.name).join(","),
  ]);

  const cache = new BuildCache(cacheDir);
  const outputs = [outputPath];
  if (!options.force && cache.isFresh(cacheKey, outputs)) {
    return { outputPath, cached: true, diagnostics: [] };
  }

  // -- front end -----------------------------------------------------------
  const file = new SourceFile(absoluteEntry, sourceText);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(file, diagnostics);
  const sourceFile = parser.parseSourceFile();
  const { ir } = generate(sourceFile, diagnostics, { builtins: registry.builtins() });

  if (options.verbose) {
    process.stderr.write(`xbintsc: generated ${ir.length} bytes of LLVM IR\n`);
  }

  if (diagnostics.hasErrors) {
    return { outputPath, cached: false, diagnostics: diagnostics.diagnostics, ir };
  }

  mkdirSync(outDir, { recursive: true });
  const irPath = resolve(outDir, basename(absoluteEntry, extname(absoluteEntry)) + ".ll");
  writeFileSync(irPath, ir);

  if (emit === "ir") {
    cache.record(cacheKey, outputs);
    cache.save();
    return { outputPath, irPath, cached: false, diagnostics: [], ir };
  }

  // -- toolchain -----------------------------------------------------------
  const clang = options.clang ?? findClang(runner);
  const runtimeDir = findRuntimeDir();
  mkdirSync(cacheDir, { recursive: true });

  const { runtimeObjects, extensionObjects } = ensureRuntimeObjects(runner, clang, runtimeDir, cacheDir, registry);

  const objectPath = emit === "obj" ? outputPath : resolve(outDir, baseName + ".o");
  compileIr(runner, { clang, irPath, objectPath, optimize });

  if (emit === "obj") {
    cache.record(cacheKey, outputs);
    cache.save();
    return { outputPath, irPath, cached: false, diagnostics: [], ir };
  }

  link(runner, {
    clang,
    objectPaths: [objectPath, ...runtimeObjects, ...extensionObjects],
    outputPath,
    linkerFlags: [...(process.platform === "win32" ? [] : ["-lm"]), ...registry.linkerFlags()],
    optimize,
  });

  cache.record(cacheKey, outputs);
  cache.save();
  return { outputPath, irPath, cached: false, diagnostics: [], ir };
}

interface RuntimeObjects {
  readonly runtimeObjects: readonly string[];
  readonly extensionObjects: readonly string[];
}

/** Core runtime translation units (each compiled and cached independently). */
const RUNTIME_SOURCES = [
  "xt_alloc.c",
  "xt_values.c",
  "xt_containers.c",
  "xt_stdlib.c",
  "xt_builtins.c",
  "xt_io.c",
] as const;

/**
 * Compile the core runtime and every extension source, reusing cached object
 * files keyed on the C source hash. Returns the object paths to link.
 */
function ensureRuntimeObjects(
  runner: Runner,
  clang: string,
  runtimeDir: string,
  cacheDir: string,
  registry: ExtensionRegistry,
): RuntimeObjects {
  const compileOne = (sourcePath: string, tag: string): string => {
    const body = readFileSync(sourcePath, "utf8");
    const key = hashParts(["runtime", COMPILER_VERSION, tag, body, clang, process.platform]);
    const objectPath = join(cacheDir, `${tag}-${key}.o`);
    if (!existsSync(objectPath)) {
      compileC(runner, clang, sourcePath, objectPath, runtimeDir);
    }
    return objectPath;
  };

  const runtimeObjects = RUNTIME_SOURCES.map((source) =>
    compileOne(join(runtimeDir, source), basename(source, ".c")),
  );
  const extensionObjects = registry
    .runtimeSources()
    .map((sourcePath, index) => compileOne(sourcePath, `ext_${index}_${basename(sourcePath, ".c")}`));
  return { runtimeObjects, extensionObjects };
}
