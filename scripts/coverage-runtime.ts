#!/usr/bin/env node
/**
 * Runtime (C) coverage for xbintsc.
 *
 * Vitest's V8 provider only sees the compiler's TypeScript sources
 * (the `src/` tree, see vitest.config.ts). The C runtime under `runtime/` is
 * invisible to it, so this script drives the *other* coverage system: clang's
 * source-based instrumentation.
 *
 * The lever is `CCC_OVERRIDE_OPTIONS`. Exporting
 * `+-fprofile-instr-generate +-fcoverage-mapping` makes clang append those
 * flags to every compile and link it performs. The driver forwards the ambient
 * environment to clang, so no xbintsc option or source change is needed to
 * instrument the runtime. Instrumented programs then write `.profraw` files
 * named by `LLVM_PROFILE_FILE`, which `llvm-profdata` and `llvm-cov` turn
 * into a per-source report.
 *
 * Two things make this work against the test suite:
 *
 *   - The e2e harness gives every suite its own temporary cache, so the
 *     instrumented runtime objects are always compiled fresh (no stale,
 *     un-instrumented object can be reused).
 *   - `llvm-cov report` needs a binary that *contains* the coverage mapping,
 *     but the harness deletes each compiled program when its suite ends. A
 *     single reference binary, built from the same runtime sources with the
 *     same clang and flags, carries the mapping for every runtime file, so the
 *     merged profile can be reported against it.
 *
 * Usage:
 *   npm run coverage:runtime                    instrument, run the tests, report
 *   npm run coverage:runtime -- --report-only   report profiles from an earlier run
 *   npm run coverage:runtime -- tests/e2e       forward args to `vitest run`
 *
 * Options:
 *   --report-only        Skip the test run; merge whatever profiles already exist.
 *   --prof-dir <dir>     Where compiled programs write .profraw (default build/runtime-prof).
 *   --out-dir <dir>      Where the report is written (default coverage/runtime).
 *   --threshold <pct>    Fail when total runtime line coverage is below <pct>.
 *   --html               Also write an HTML report under <out-dir>/html.
 *   --keep-raw           Keep the .profraw files after merging.
 *   --strict             Fail (instead of skipping) when the LLVM tools are missing.
 *   -h, --help           Show this help.
 */

import { existsSync, mkdirSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { spawnSync, type SpawnSyncOptions } from "node:child_process";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "../src/driver/compiler.js";
import { createDefaultRegistry } from "../src/extensions/registry.js";
import { nodeExtension } from "../src/extensions/node/index.js";
import { findRuntimeDir } from "../src/driver/paths.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

/** Flags clang appends to every compile/link in an instrumented build. */
const INSTRUMENT = "+-fprofile-instr-generate +-fcoverage-mapping";

interface Options {
  reportOnly: boolean;
  profDir: string;
  outDir: string;
  threshold?: number;
  html: boolean;
  keepRaw: boolean;
  strict: boolean;
  testArgs: string[];
}

interface CommandResult {
  status: number;
  stdout: string;
  stderr: string;
}

function fail(message: string): never {
  console.error(`coverage-runtime: ${message}`);
  process.exit(1);
}

function skip(message: string, strict: boolean): never {
  if (strict) fail(message);
  console.warn(`coverage-runtime: skipping - ${message}`);
  process.exit(0);
}

function usage(): void {
  process.stdout.write(
    [
      "Usage: npm run coverage:runtime [-- options] [-- vitest args]",
      "",
      "  --report-only      merge profiles from an earlier instrumented run",
      "  --prof-dir <dir>   .profraw output directory (default build/runtime-prof)",
      "  --out-dir <dir>    report directory (default coverage/runtime)",
      "  --threshold <pct>  fail below this total line coverage",
      "  --html             also write an HTML report",
      "  --keep-raw         keep .profraw files after merging",
      "  --strict           fail when the LLVM tools are missing",
      "",
    ].join("\n"),
  );
}

function parseArgs(argv: readonly string[]): Options {
  const options: Options = {
    reportOnly: false,
    profDir: join(root, "build", "runtime-prof"),
    outDir: join(root, "coverage", "runtime"),
    html: false,
    keepRaw: false,
    strict: false,
    testArgs: [],
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index]!;
    if (arg === "--report-only") options.reportOnly = true;
    else if (arg === "--html") options.html = true;
    else if (arg === "--keep-raw") options.keepRaw = true;
    else if (arg === "--strict") options.strict = true;
    else if (arg === "--prof-dir") options.profDir = resolve(requireValue(argv, (index += 1), arg));
    else if (arg === "--out-dir") options.outDir = resolve(requireValue(argv, (index += 1), arg));
    else if (arg === "--threshold") {
      const value = requireValue(argv, (index += 1), arg);
      const parsed = Number(value);
      if (!Number.isFinite(parsed)) fail(`--threshold expects a number, got '${value}'`);
      options.threshold = parsed;
    } else if (arg === "-h" || arg === "--help") {
      usage();
      process.exit(0);
    } else if (arg.startsWith("--")) {
      fail(`unknown option '${arg}' (see --help)`);
    } else {
      options.testArgs.push(arg);
    }
  }
  return options;
}

function requireValue(argv: readonly string[], index: number, flag: string): string {
  const value = argv[index];
  if (value === undefined) fail(`${flag} needs a value`);
  return value;
}

function run(command: string, args: readonly string[], options: SpawnSyncOptions = {}): CommandResult {
  const result = spawnSync(command, [...args], { encoding: "utf8", maxBuffer: 512 * 1024 * 1024, ...options });
  return {
    status: result.status ?? (result.error ? 1 : 0),
    stdout: result.stdout ?? "",
    stderr: result.stderr ?? (result.error ? String(result.error.message) : ""),
  };
}

/** True when `command` resolves and answers `--version` successfully. */
function runs(command: string): boolean {
  const result = spawnSync(command, ["--version"], { encoding: "utf8" });
  return !result.error && result.status === 0;
}

/**
 * Locate the LLVM tool `name` (`llvm-profdata`/`llvm-cov`) that matches the
 * clang the driver resolved. Prefer the tool next to clang, then the
 * version-suffixed system installs: mixing versions makes `llvm-profdata`
 * reject the profile format.
 */
function findTool(clang: string, name: string): string | undefined {
  const exe = process.platform === "win32" ? ".exe" : "";
  const candidates: string[] = [];
  const directory = dirname(clang);
  if (directory !== "." && directory !== "") candidates.push(join(directory, name + exe));
  const version = run(clang, ["--version"]);
  const major = /version (\d+)/.exec(`${version.stdout}${version.stderr}`)?.[1];
  if (major) candidates.push(`${name}-${major}`, `${name}-${major}${exe}`);
  candidates.push(name + exe, name);
  for (const version of ["18", "19", "20", "21", "22", "23"]) {
    candidates.push(`${name}-${version}`, `${name}-${version}${exe}`);
  }
  for (const candidate of candidates) {
    if (runs(candidate)) return candidate;
  }
  if (process.platform === "darwin") {
    // The Command Line Tools expose the tools through `xcrun` rather than PATH.
    const found = spawnSync("xcrun", ["--find", name], { encoding: "utf8" });
    const path = (found.stdout ?? "").trim();
    if (!found.error && found.status === 0 && path.length > 0 && runs(path)) return path;
  }
  return undefined;
}

function collectProfiles(directory: string): string[] {
  if (!existsSync(directory)) return [];
  return readdirSync(directory)
    .filter((name) => name.endsWith(".profraw"))
    .sort()
    .map((name) => join(directory, name));
}

/** Merge profiles, chunking the input so a huge list cannot overflow argv. */
function mergeProfiles(tool: string, inputs: readonly string[], output: string, scratch: string): void {
  const chunkSize = 128;
  if (inputs.length <= chunkSize) {
    const result = run(tool, ["merge", "-sparse", ...inputs, "-o", output]);
    if (result.status !== 0) fail(`llvm-profdata merge failed:\n${result.stderr}`);
    return;
  }
  const parts: string[] = [];
  for (let index = 0; index < inputs.length; index += chunkSize) {
    const part = join(scratch, `part-${parts.length}.profdata`);
    const result = run(tool, ["merge", "-sparse", ...inputs.slice(index, index + chunkSize), "-o", part]);
    if (result.status !== 0) fail(`llvm-profdata merge failed:\n${result.stderr}`);
    parts.push(part);
  }
  const result = run(tool, ["merge", "-sparse", ...parts, "-o", output]);
  if (result.status !== 0) fail(`llvm-profdata merge failed:\n${result.stderr}`);
  for (const part of parts) rmSync(part, { force: true });
}

/**
 * Build the binary whose embedded coverage mapping the merged profile is
 * reported against. It must be instrumented with the same clang and flags as
 * the test-produced binaries, and it carries the Node extension so that
 * extension's C sources are covered too.
 */
function buildReference(directory: string): string {
  mkdirSync(directory, { recursive: true });
  const entry = join(directory, "reference.ts");
  writeFileSync(entry, "console.log(1);\n");
  const registry = createDefaultRegistry().register(nodeExtension);
  const result = build(entry, {
    emit: "exe",
    outDir: directory,
    cacheDir: join(directory, "cache"),
    extensions: registry,
    preferPrebuilt: false,
    force: true,
  });
  const errors = result.diagnostics.filter((diagnostic) => diagnostic.category === "error");
  if (errors.length > 0) fail(`reference build failed:\n${errors.map((e) => e.message).join("\n")}`);
  return result.outputPath;
}

function runTests(options: Options): void {
  const vitest = join(root, "node_modules", "vitest", "vitest.mjs");
  if (!existsSync(vitest)) fail("vitest is not installed; run 'npm install' first");
  rmSync(options.profDir, { recursive: true, force: true });
  mkdirSync(options.profDir, { recursive: true });
  console.log("coverage-runtime: running the test suite with instrumented clang");
  const result = spawnSync(process.execPath, [vitest, "run", ...options.testArgs], {
    stdio: "inherit",
    cwd: root,
    env: {
      ...process.env,
      LLVM_PROFILE_FILE: join(options.profDir, "%p-%m.profraw"),
      xbintsc_CACHE_DIR: join(root, "build", "runtime-cov-cache"),
      // A prebuilt runtime/lib archive is not instrumented, so force the driver
      // to compile (and thus instrument) the C sources instead.
      xbintsc_PREFER_PREBUILT: "0",
    },
  });
  if (result.status !== 0) fail(`tests failed (exit ${result.status ?? "unknown"})`);
}

function main(): void {
  const options = parseArgs(process.argv.slice(2));

  // Set once, before anything shells out to clang, so both the test run and the
  // reference build are instrumented even in --report-only mode.
  const existing = process.env.CCC_OVERRIDE_OPTIONS;
  process.env.CCC_OVERRIDE_OPTIONS = existing ? `${existing} ${INSTRUMENT}` : INSTRUMENT;

  let clang: string;
  try {
    clang = resolveToolchain().clang;
  } catch (error) {
    skip(`no clang-compatible compiler available (${String(error)})`, options.strict);
  }
  const profdataTool = findTool(clang, "llvm-profdata");
  const covTool = findTool(clang, "llvm-cov");
  if (!profdataTool || !covTool) {
    const missing = [
      profdataTool ? undefined : "llvm-profdata",
      covTool ? undefined : "llvm-cov",
    ].filter(Boolean).join(", ");
    skip(`${missing} not found for ${clang}; install the LLVM tools (llvm-profdata, llvm-cov)`, options.strict);
  }

  if (!options.reportOnly) runTests(options);

  const profiles = collectProfiles(options.profDir);
  if (profiles.length === 0) {
    skip(`no .profraw files in ${relative(root, options.profDir)} (did the tests run?)`, options.strict);
  }
  console.log(`coverage-runtime: merging ${profiles.length} profile(s)`);

  mkdirSync(options.outDir, { recursive: true });
  const referenceDir = join(root, "build", "runtime-coverage");
  const reference = buildReference(referenceDir);
  const profdata = join(options.outDir, "runtime.profdata");
  mergeProfiles(profdataTool!, profiles, profdata, options.outDir);

  const runtimeDir = findRuntimeDir();
  const report = run(covTool!, ["report", reference, `--instr-profile=${profdata}`, runtimeDir]);
  if (report.status !== 0) fail(`llvm-cov report failed:\n${report.stderr}`);
  writeFileSync(join(options.outDir, "report.txt"), report.stdout);
  process.stdout.write(report.stdout);

  const summary = run(covTool!, ["export", reference, `--instr-profile=${profdata}`, "--summary-only"]);
  if (summary.status !== 0) fail(`llvm-cov export failed:\n${summary.stderr}`);
  const parsed = JSON.parse(summary.stdout) as {
    data?: { totals?: { lines?: { percent?: number } } }[];
  };
  const percent = parsed.data?.[0]?.totals?.lines?.percent ?? 0;
  console.log(`coverage-runtime: runtime line coverage ${percent.toFixed(2)}%`);
  console.log(`coverage-runtime: report -> ${relative(root, join(options.outDir, "report.txt"))}`);

  if (options.html) {
    const htmlDir = join(options.outDir, "html");
    rmSync(htmlDir, { recursive: true, force: true });
    const shown = run(covTool!, [
      "show",
      reference,
      `--instr-profile=${profdata}`,
      "--format=html",
      `--output-dir=${htmlDir}`,
      runtimeDir,
    ]);
    if (shown.status !== 0) fail(`llvm-cov show failed:\n${shown.stderr}`);
    console.log(`coverage-runtime: HTML report -> ${relative(root, htmlDir)}`);
  }

  if (!options.keepRaw) rmSync(options.profDir, { recursive: true, force: true });
  if (options.threshold !== undefined && percent < options.threshold) {
    fail(`runtime line coverage ${percent.toFixed(2)}% is below the ${options.threshold}% threshold`);
  }
}

main();
