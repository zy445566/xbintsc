/**
 * The build driver: source file in, native binary out.
 *
 * Pipeline: read -> parse -> bind/check -> LLVM IR -> object -> link.
 * The runtime (and any registered extension sources) are compiled once and
 * cached, and the whole build is skipped when the incremental cache says
 * nothing relevant changed.
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, extname, join, resolve } from "node:path";
import { DiagnosticBag, type Diagnostic } from "../diagnostics/diagnostic.js";
import { SourceFile } from "../diagnostics/source.js";
import { Parser } from "../parser/parser.js";
import { generate } from "../codegen/llvm.js";
import { SyntaxKind } from "../ast/nodes.js";
import { bundleModules } from "./modules.js";
import { createDefaultRegistry, type ExtensionRegistry } from "../extensions/registry.js";
import { BuildCache, hashParts } from "./cache.js";
import { findRuntimeDir } from "./paths.js";
import { findRuntimeLibrary } from "./runtime-lib.js";
import { resolveToolchain } from "./toolchain-provider.js";
import {
  compileC,
  compileIr,
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
  /** Prefer a prebuilt `runtime/lib/<os>-<arch>/*.a` archive when present (default true). */
  readonly preferPrebuilt?: boolean;
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
  const { ir } = generate(sourceFile, diagnostics, {
    builtins: registry.builtins(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
  });
  return { ir, diagnostics: diagnostics.diagnostics };
}

/**
 * Every module specifier the registry can resolve at code generation time (the
 * registered platform modules plus the hints for known-but-disabled
 * extensions). Bare imports outside this set are looked up in `node_modules`
 * and bundled as source.
 */
function externalModuleSpecifiers(registry: ExtensionRegistry): Set<string> {
  return new Set([
    ...Object.keys(registry.modules()),
    ...Object.keys(registry.moduleHints()),
  ]);
}

/**
 * Compile a file to LLVM IR, bundling every reachable relative module first so
 * that imports from other TypeScript sources resolve exactly like `build`.
 */
export function compileEntry(entryPath: string, extensions?: ExtensionRegistry): CompileStringResult {
  const absoluteEntry = resolve(entryPath);
  const sourceText = readFileSync(absoluteEntry, "utf8");
  const file = new SourceFile(absoluteEntry, sourceText);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(file, diagnostics);
  let sourceFile = parser.parseSourceFile();
  const registry = extensions ?? createDefaultRegistry();
  const isModule = sourceFile.statements.some(
    (statement) =>
      statement.kind === SyntaxKind.ImportDeclaration ||
      statement.kind === SyntaxKind.ExportDeclaration ||
      statement.kind === SyntaxKind.ExportAssignment,
  );
  if (isModule) {
    const bundled = bundleModules(absoluteEntry, diagnostics, externalModuleSpecifiers(registry));
    if (bundled) sourceFile = bundled.sourceFile;
  }
  const { ir } = generate(sourceFile, diagnostics, {
    builtins: registry.builtins(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
  });
  return { ir, diagnostics: diagnostics.diagnostics };
}

/**
 * Where the incremental cache lives when the caller does not name a directory:
 * `xbintsc_CACHE_DIR` when set, otherwise `<cwd>/.xbintsc`. The override keeps
 * an instrumented build (see `scripts/coverage-runtime.ts`) from reusing the
 * un-instrumented objects in a normal cache, which would silently yield no
 * profile data.
 */
function defaultCacheDir(): string {
  return process.env.xbintsc_CACHE_DIR || join(process.cwd(), ".xbintsc");
}

/**
 * Whether a build may link a prebuilt `runtime/lib` archive. The explicit option
 * wins; otherwise `xbintsc_PREFER_PREBUILT` decides (`0`/`false` disables) and
 * the default is to use an archive when one exists. Instrumented builds set it
 * to `0` so the C runtime is compiled (and therefore instrumented) from source.
 */
export function resolvePreferPrebuilt(explicit: boolean | undefined): boolean {
  if (explicit !== undefined) return explicit;
  const raw = process.env.xbintsc_PREFER_PREBUILT;
  if (raw === undefined || raw === "") return true;
  return raw !== "0" && raw.toLowerCase() !== "false";
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
  const cacheDir = resolve(options.cacheDir ?? defaultCacheDir());
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

  // -- front end -----------------------------------------------------------
  const file = new SourceFile(absoluteEntry, sourceText);
  const diagnostics = new DiagnosticBag();
  const parser = new Parser(file, diagnostics);
  let sourceFile = parser.parseSourceFile();

  // Lower `import`/`export` by merging every reachable module into one file.
  const isModule = sourceFile.statements.some(
    (statement) =>
      statement.kind === SyntaxKind.ImportDeclaration ||
      statement.kind === SyntaxKind.ExportDeclaration ||
      statement.kind === SyntaxKind.ExportAssignment,
  );
  let cacheText = sourceText;
  if (isModule) {
    const bundled = bundleModules(absoluteEntry, diagnostics, externalModuleSpecifiers(registry));
    if (bundled) {
      sourceFile = bundled.sourceFile;
      cacheText = bundled.text;
    }
  }

  const runtimeDir = findRuntimeDir();
  const cacheKey = hashParts([
    COMPILER_VERSION,
    cacheText,
    emit,
    optimize,
    process.platform,
    registryFingerprint(registry),
    runtimeFingerprint(runtimeDir),
  ]);

  const cache = new BuildCache(cacheDir);
  const outputs = [outputPath];
  if (!options.force && !diagnostics.hasErrors && cache.isFresh(cacheKey, outputs)) {
    return { outputPath, cached: true, diagnostics: [] };
  }

  const { ir } = generate(sourceFile, diagnostics, {
    builtins: registry.builtins(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
  });

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
  const toolchain = options.clang
    ? { clang: options.clang, linkerArgs: [] as string[], env: {} as Record<string, string> }
    : resolveToolchain(runner);
  const clang = toolchain.clang;
  mkdirSync(cacheDir, { recursive: true });

  const objectPath = emit === "obj" ? outputPath : resolve(outDir, baseName + ".o");
  compileIr(runner, { clang, irPath, objectPath, optimize, env: toolchain.env });

  if (emit === "obj") {
    cache.record(cacheKey, outputs);
    cache.save();
    return { outputPath, irPath, cached: false, diagnostics: [], ir };
  }

  const { runtimeObjects, extensionObjects, nativeObjects } = ensureRuntimeObjects(
    runner,
    clang,
    runtimeDir,
    cacheDir,
    registry,
    resolvePreferPrebuilt(options.preferPrebuilt),
    toolchain.env,
  );

  link(runner, {
    clang,
    objectPaths: [objectPath, ...runtimeObjects, ...extensionObjects, ...nativeObjects],
    outputPath,
    linkerFlags: [
      ...toolchain.linkerArgs,
      ...(process.platform === "win32" ? ["-lws2_32"] : ["-lm"]),
      ...registry.linkerFlags(),
    ],
    optimize,
    env: toolchain.env,
  });

  cache.record(cacheKey, outputs);
  cache.save();
  return { outputPath, irPath, cached: false, diagnostics: [], ir };
}

interface RuntimeObjects {
  readonly runtimeObjects: readonly string[];
  readonly extensionObjects: readonly string[];
  /** Pre-built C++/Rust objects and archives contributed by native extensions. */
  readonly nativeObjects: readonly string[];
}

/** Core runtime translation units (each compiled and cached independently). */
export const RUNTIME_SOURCES = [
  "xt_alloc.c",
  "xt_values.c",
  "xt_bigint.c",
  "xt_containers.c",
  "xt_stdlib.c",
  "xt_stdlib2.c",
  "xt_promise.c",
  "xt_loop.c",
  "xt_generator.c",
  "xt_symbol.c",
  "xt_builtins.c",
  "xt_io.c",
] as const;

/**
 * Fingerprint every runtime artifact that can change a linked executable: the
 * C sources, shared headers and `#include`d `.inc` fragments plus the shipped
 * prebuilt archives. It feeds the executable cache key, so a runtime change
 * invalidates a cached binary even when the entry source is untouched — the
 * per-object cache cannot express that, because the freshness check runs before
 * any compilation.
 */
function runtimeFingerprint(runtimeDir: string): string {
  if (!existsSync(runtimeDir)) return "";
  const parts: string[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (/\.(c|h|inc)$/.test(entry.name)) {
        parts.push(entry.name, readFileSync(full, "utf8"));
      } else {
        parts.push(entry.name, readFileSync(full).toString("base64"));
      }
    }
  };
  walk(runtimeDir);
  return hashParts(parts);
}

/**
 * Fingerprint everything an extension contributes that can change the linked
 * executable but does not live under `runtime/`: its name, linker flags and the
 * contents of any pre-built C++/Rust objects. Keeps a cached binary fresh when
 * a native extension is rebuilt, or when `--ext-native` flags change.
 */
function registryFingerprint(registry: ExtensionRegistry): string {
  const parts: string[] = [];
  for (const extension of registry.all()) {
    parts.push(extension.name);
    parts.push(...(extension.linkerFlags?.() ?? []));
    for (const object of extension.nativeObjects?.() ?? []) {
      parts.push(object, existsSync(object) ? readFileSync(object).toString("base64") : "");
    }
  }
  return hashParts(parts);
}

/**
 * Compile the core runtime and every extension source, reusing cached object
 * files keyed on the C source hash. Returns the object paths to link.
 *
 * When `preferPrebuilt` is set, a shipped static archive
 * (`runtime/lib/<os>-<arch>/{core,ext_<name>}.a`) is used instead of compiling
 * that group's C sources. Missing archives fall back to compiling the sources.
 */
function ensureRuntimeObjects(
  runner: Runner,
  clang: string,
  runtimeDir: string,
  cacheDir: string,
  registry: ExtensionRegistry,
  preferPrebuilt: boolean,
  env: Record<string, string>,
): RuntimeObjects {
  /* Runtime objects depend on the shared headers (rt.h/rt_internal.h/...) and
     the `#include`d implementation fragments (`.inc`), so any change to them
     must invalidate every cached object. Hash them all. */
  const headerParts: string[] = [];
  const collectHeaders = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) collectHeaders(full);
      else if (entry.name.endsWith(".h") || entry.name.endsWith(".inc")) headerParts.push(readFileSync(full, "utf8"));
    }
  };
  collectHeaders(runtimeDir);
  const headerStamp = hashParts(headerParts);

  const compileOne = (sourcePath: string, tag: string): string => {
    const body = readFileSync(sourcePath, "utf8");
    const key = hashParts(["runtime", COMPILER_VERSION, tag, headerStamp, body, clang, process.platform]);
    const objectPath = join(cacheDir, `${tag}-${key}.o`);
    if (!existsSync(objectPath)) {
      compileC(runner, clang, sourcePath, objectPath, runtimeDir, env);
    }
    return objectPath;
  };

  const coreArchive = preferPrebuilt ? findRuntimeLibrary("core") : undefined;
  const runtimeObjects = coreArchive
    ? [coreArchive]
    : RUNTIME_SOURCES.map((source) => compileOne(join(runtimeDir, source), basename(source, ".c")));

  const extensionObjects: string[] = [];
  const seen = new Set<string>();
  for (const extension of registry.all()) {
    const sources = extension.runtimeSources ? extension.runtimeSources() : [];
    if (sources.length === 0) continue;
    const archive = preferPrebuilt ? findRuntimeLibrary(`ext_${extension.name}`) : undefined;
    if (archive) {
      extensionObjects.push(archive);
      continue;
    }
    for (const sourcePath of sources) {
      const dedupeKey = resolve(sourcePath);
      if (seen.has(dedupeKey)) continue;
      seen.add(dedupeKey);
      extensionObjects.push(compileOne(sourcePath, `ext_${extensionObjects.length}_${basename(sourcePath, ".c")}`));
    }
  }
  // A native archive may be shared by several manifests; link each once.
  return { runtimeObjects, extensionObjects, nativeObjects: [...new Set(registry.nativeObjects())] };
}
