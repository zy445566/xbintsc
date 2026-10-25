/**
 * The build driver: source file in, native binary out.
 *
 * Pipeline: read -> parse -> bind/check -> LLVM IR -> object -> link.
 * The runtime (and any registered extension sources) are compiled once and
 * cached, and the whole build is skipped when the incremental cache says
 * nothing relevant changed.
 *
 * This module only orchestrates the phases. The front end lives in
 * `frontend.ts`, cache-key inputs in `build-key.ts`, runtime compilation in
 * `runtime-objects.ts`, platform artifacts in `app-artifacts.ts`, and the
 * version constant in `version.ts`.
 */

import { mkdirSync, writeFileSync } from "node:fs";
import { basename, dirname, extname, join, resolve } from "node:path";
import { DiagnosticBag, DiagnosticCode } from "../diagnostics/diagnostic.js";
import { generate } from "../codegen/llvm.js";
import { createDefaultRegistry } from "../extensions/registry.js";
import { BuildCache } from "./cache.js";
import { findRuntimeDir } from "./paths.js";
import { resolveToolchain } from "./toolchain-provider.js";
import { readIcon, IconError, type IconInfo } from "./icon.js";
import { bundlePathFor, packageMacApp } from "./mac-bundle.js";
import type { AppConfig } from "./config.js";
import { compileIr, link, mergeProfileData, realRunner, ToolchainError } from "./toolchain.js";
import { parseEntry } from "./frontend.js";
import { ensureRuntimeObjects } from "./runtime-objects.js";
import { ensureAppArtifacts } from "./app-artifacts.js";
import { buildCacheKey } from "./build-key.js";
import type { BuildOptions, BuildResult, EmitKind, PhaseTiming } from "./options.js";

export { COMPILER_VERSION } from "./version.js";
export { compileString, compileEntry, parseEntry, type FrontendOptions, type ParsedEntry } from "./frontend.js";
export { RUNTIME_SOURCES, type RuntimeObjects } from "./runtime-objects.js";
export type { BuildOptions, BuildResult, CompileStringResult, EmitKind, PhaseTiming } from "./options.js";

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
  registry.validate();
  const outDir = resolve(options.outDir ?? join(process.cwd(), "build"));
  const cacheDir = resolve(options.cacheDir ?? defaultCacheDir());
  const emit: EmitKind = options.emit ?? "exe";
  const optimize = options.optimize ?? "2";
  const lto = options.lto === true;
  const pgo = options.pgo;
  const pgoDir = resolve(options.pgoDir ?? join(outDir, "pgo"));
  if (pgo) mkdirSync(pgoDir, { recursive: true });

  const absoluteEntry = resolve(entryPath);
  const buildStarted = Date.now();
  const timings: PhaseTiming[] = [];

  const baseName = basename(absoluteEntry, extname(absoluteEntry));
  const outputPath =
    emit === "ir"
      ? resolve(options.output ?? join(outDir, baseName + ".ll"))
      : emit === "obj"
        ? resolve(options.output ?? join(outDir, baseName + ".o"))
        : executableName(absoluteEntry, outDir, options.output);

  // -- front end -----------------------------------------------------------
  const diagnostics = new DiagnosticBag();
  const parsed = parseEntry(absoluteEntry, registry, diagnostics);
  timings.push({ phase: "frontend", ms: Date.now() - buildStarted });

  /* Parse (or module-graph) errors leave missing nodes in the tree. Stop before
   * code generation instead of letting the binder crash on them. */
  if (diagnostics.hasErrors) {
    return { outputPath, cached: false, diagnostics: diagnostics.diagnostics, timings };
  }

  const app: AppConfig = options.app ?? {};
  const iconPath = options.icon ?? app.icon;
  let icon: IconInfo | undefined;
  if (iconPath) {
    try {
      icon = readIcon(iconPath);
    } catch (error) {
      if (error instanceof IconError) {
        diagnostics.error(DiagnosticCode.IOError, error.message);
        return { outputPath, cached: false, diagnostics: diagnostics.diagnostics };
      }
      throw error;
    }
  }

  const runtimeDir = findRuntimeDir();
  const cacheKey = buildCacheKey({
    sourceText: parsed.cacheText,
    emit,
    optimize,
    registry,
    runtimeDir,
    icon,
    app,
    debug: options.debug === true,
    lto,
    check: options.check !== false,
    pgo,
    pgoDir,
  });

  // A `.app` bundle is deterministic from the output path and name, so it can be
  // part of the cache key before the bundle actually exists.
  const bundle =
    process.platform === "darwin" && emit === "exe" && app.bundle === true
      ? bundlePathFor(outputPath, app.name ?? baseName)
      : undefined;
  const cache = new BuildCache(cacheDir);
  const outputs = bundle ? [outputPath, bundle] : [outputPath];
  /* PGO profile data can change without the cache key changing, so always
   * rebuild a `--pgo-use` binary; it is an explicit optimization step. */
  if (!options.force && pgo !== "use" && !diagnostics.hasErrors && cache.isFresh(cacheKey, outputs)) {
    return { outputPath, cached: true, diagnostics: [], timings, ...(bundle ? { bundlePath: bundle } : {}) };
  }

  const codegenStarted = Date.now();
  const { ir } = generate(parsed.sourceFile, diagnostics, {
    builtins: registry.builtins(),
    globalValues: registry.globalValues(),
    modules: registry.modules(),
    moduleHints: registry.moduleHints(),
    debug: options.debug === true,
    check: options.check !== false,
  });
  timings.push({ phase: "codegen", ms: Date.now() - codegenStarted });

  if (options.verbose) {
    process.stderr.write(`xbintsc: generated ${ir.length} bytes of LLVM IR\n`);
  }

  if (diagnostics.hasErrors) {
    return { outputPath, cached: false, diagnostics: diagnostics.diagnostics, ir, timings };
  }

  mkdirSync(outDir, { recursive: true });
  /* An explicit `-o path/to/app.exe` (or `output` in the config) may name a
   * directory that does not exist yet: the IR and object files land in `outDir`,
   * but clang writes the *executable* to `outputPath`, and a missing parent
   * directory there makes the link fail with `LNK1104: cannot open file`. */
  mkdirSync(dirname(outputPath), { recursive: true });
  const irPath = resolve(outDir, basename(absoluteEntry, extname(absoluteEntry)) + ".ll");
  writeFileSync(irPath, ir);

  if (emit === "ir") {
    cache.record(cacheKey, outputs);
    cache.save();
    return { outputPath, irPath, cached: false, diagnostics: [], ir, timings };
  }

  // -- toolchain -----------------------------------------------------------
  const clangStarted = Date.now();
  const toolchain = options.clang
    ? { clang: options.clang, linkerArgs: [] as string[], env: {} as Record<string, string> }
    : resolveToolchain(runner);
  const clang = toolchain.clang;
  if (pgo === "use") {
    try {
      mergeProfileData(runner, pgoDir, clang, toolchain.env);
    } catch (error) {
      if (error instanceof ToolchainError) {
        diagnostics.error(DiagnosticCode.ToolchainError, error.message);
        return { outputPath, irPath, cached: false, diagnostics: diagnostics.diagnostics, ir };
      }
      throw error;
    }
  }
  mkdirSync(cacheDir, { recursive: true });

  const objectPath = emit === "obj" ? outputPath : resolve(outDir, baseName + ".o");
  compileIr(runner, {
    clang,
    irPath,
    objectPath,
    optimize,
    env: toolchain.env,
    debug: options.debug === true,
    lto,
    ...(pgo ? { pgo, pgoDir } : {}),
  });
  timings.push({ phase: "clang", ms: Date.now() - clangStarted });

  if (emit === "obj") {
    cache.record(cacheKey, outputs);
    cache.save();
    return { outputPath, irPath, cached: false, diagnostics: [], ir, timings };
  }

  const runtimeStarted = Date.now();
  const { runtimeObjects, extensionObjects, nativeObjects } = ensureRuntimeObjects({
    runner,
    clang,
    runtimeDir,
    cacheDir,
    registry,
    preferPrebuilt: resolvePreferPrebuilt(options.preferPrebuilt) && !lto && pgo === undefined,
    env: toolchain.env,
    lto,
    pgo,
    pgoDir,
  });
  timings.push({ phase: "runtime", ms: Date.now() - runtimeStarted });

  const artifacts = ensureAppArtifacts({
    runner,
    clang,
    cacheDir,
    registry,
    app,
    icon,
    env: toolchain.env,
    verbose: options.verbose,
  });
  if (artifacts.iconError !== undefined) {
    diagnostics.error(DiagnosticCode.ToolchainError, artifacts.iconError);
    return { outputPath, irPath, cached: false, diagnostics: diagnostics.diagnostics, ir };
  }

  const linkStarted = Date.now();
  link(runner, {
    clang,
    objectPaths: [
      objectPath,
      ...runtimeObjects,
      ...extensionObjects,
      ...nativeObjects,
      ...(artifacts.entryObject !== undefined ? [artifacts.entryObject] : []),
      ...artifacts.iconObjects,
    ],
    outputPath,
    linkerFlags: [
      ...toolchain.linkerArgs,
      ...(process.platform === "win32" ? ["-lws2_32"] : ["-lm", "-pthread"]),
      ...registry.linkerFlags(),
      /* A GUI program is a Windows-subsystem executable: no console window is
       * created when it is launched. Console output (stdout/stderr) is lost in
       * that subsystem, so a program that wants to report things opts back in
       * with `app.console: true`. */
      ...artifacts.linkerFlags,
    ],
    optimize,
    env: toolchain.env,
    lto,
    ...(pgo ? { pgo, pgoDir } : {}),
  });
  timings.push({ phase: "link", ms: Date.now() - linkStarted });

  const bundlePath =
    bundle !== undefined
      ? packageMacApp({
          executablePath: outputPath,
          name: app.name ?? baseName,
          bundleId: app.bundleId ?? defaultBundleId(baseName),
          ...(icon ? { icon } : {}),
          runner,
          env: toolchain.env,
        })
      : undefined;

  cache.record(cacheKey, outputs);
  cache.save();
  return { outputPath, irPath, cached: false, diagnostics: [], ir, timings, ...(bundlePath ? { bundlePath } : {}) };
}

/** Default `CFBundleIdentifier` for a bundle when none is configured. */
function defaultBundleId(name: string): string {
  const sanitized = name.replace(/[^A-Za-z0-9.-]/g, "");
  return `com.xbintsc.${sanitized || "app"}`;
}
