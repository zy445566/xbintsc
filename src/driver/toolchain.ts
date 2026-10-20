/**
 * Thin wrappers around the external toolchain (clang) and process execution.
 *
 * Everything that shells out lives here so the rest of the driver stays
 * testable: unit tests can pass a fake `Runner` and never touch the host.
 */

import { spawnSync, type SpawnSyncOptions } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

export interface CommandResult {
  readonly status: number;
  readonly stdout: string;
  readonly stderr: string;
}

export interface RunOptions {
  readonly cwd?: string;
  /** Extra environment variables, merged over `process.env`. */
  readonly env?: Record<string, string>;
}

export interface Runner {
  run(command: string, args: readonly string[], options?: RunOptions): CommandResult;
}

export const realRunner: Runner = {
  run(command, args, options = {}) {
    const spawnOptions: SpawnSyncOptions = {
      cwd: options.cwd,
      encoding: "utf8",
      maxBuffer: 64 * 1024 * 1024,
    };
    if (options.env) spawnOptions.env = Object.assign({}, process.env, options.env);
    const result = spawnSync(command, [...args], spawnOptions);
    return {
      status: result.status ?? (result.error ? 1 : 0),
      stdout: result.stdout?.toString() ?? "",
      stderr: result.stderr?.toString() ?? (result.error ? String(result.error.message) : ""),
    };
  },
};

export class ToolchainError extends Error {
  readonly command: string;
  readonly status: number;
  readonly stderr: string;
  constructor(command: string, status: number, stderr: string) {
    super(`Command failed (${status}): ${command}\n${stderr}`);
    this.name = "ToolchainError";
    this.command = command;
    this.status = status;
    this.stderr = stderr;
  }
}

/**
 * The oldest clang/LLVM the emitted IR is known to compile with. LLVM 16 made
 * opaque pointers the only pointer representation, and the IR emitted by
 * `src/codegen` relies on that; clang 15 and older reject it with
 * "'@.str.0' defined with type '[6 x i8]*' but expected 'i8*'".
 */
export const MINIMUM_CLANG_MAJOR = 16;

export interface ClangVersion {
  readonly major: number;
  readonly minor: number;
  readonly patch: number;
}

/** Result of probing one candidate with `--version`. */
export interface ClangProbe {
  readonly command: string;
  /** True when the command ran and exited 0. */
  readonly responds: boolean;
  readonly version?: ClangVersion;
  /** First line of the version output, for diagnostics. */
  readonly summary: string;
}

export interface ClangLocation {
  readonly command: string;
  readonly source: "env" | "system";
  /** Absent when an explicit wrapper does not print a parseable version. */
  readonly version?: ClangVersion;
}

/**
 * Extract the LLVM version from a `--version` line. Accepts "clang version
 * 19.1.5", "Apple clang version 15.0.0" and distribution prefixes such as
 * "Ubuntu clang version 18.1.3"; returns `undefined` for anything else (notably
 * gcc, which `cc` may resolve to).
 */
export function parseClangVersion(output: string): ClangVersion | undefined {
  const match = /clang version (\d+)\.(\d+)\.(\d+)/i.exec(output);
  if (!match) return undefined;
  return { major: Number(match[1]), minor: Number(match[2]), patch: Number(match[3]) };
}

function firstLineOf(text: string): string {
  const end = text.indexOf("\n");
  return (end === -1 ? text : text.slice(0, end)).trim();
}

/** Run `<command> --version` and classify the result. Never throws. */
export function probeClang(
  runner: Runner,
  command: string,
  env?: Record<string, string>,
): ClangProbe {
  try {
    const result = runner.run(command, ["--version"], env ? { env } : undefined);
    const output = `${result.stdout}\n${result.stderr}`;
    const version = parseClangVersion(output);
    return {
      command,
      responds: result.status === 0,
      ...(version ? { version } : {}),
      summary: firstLineOf(result.stdout) || firstLineOf(result.stderr),
    };
  } catch {
    return { command, responds: false, summary: "" };
  }
}

/**
 * Visual Studio ships its own LLVM build (`VC\Tools\Llvm\x64\bin\clang.exe`).
 * It is a perfectly good driver but is not on `PATH` in a plain shell, so probe
 * the well-known install locations too. Only candidates that exist are
 * returned, and only on Windows.
 */
function visualStudioClangCandidates(): string[] {
  if (process.platform !== "win32") return [];
  const roots = [process.env.ProgramFiles, process.env["ProgramFiles(x86)"], process.env.ProgramW6432]
    .filter((root): root is string => Boolean(root));
  const arch = process.arch === "arm64" ? "ARM64" : "x64";
  const candidates: string[] = [];
  for (const root of roots) {
    for (const year of ["2022", "2019"]) {
      for (const edition of ["Community", "Professional", "Enterprise", "BuildTools"]) {
        const llvm = join(root, "Microsoft Visual Studio", year, edition, "VC", "Tools", "Llvm");
        candidates.push(join(llvm, arch, "bin", "clang.exe"));
        candidates.push(join(llvm, "bin", "clang.exe"));
      }
    }
    candidates.push(join(root, "LLVM", "bin", "clang.exe"));
  }
  return [...new Set(candidates)].filter((candidate) => existsSync(candidate));
}

/** Commands probed for a system compiler, in preference order. */
export function systemClangCandidates(): string[] {
  const candidates = ["clang"];
  for (let major = 20; major >= MINIMUM_CLANG_MAJOR; major--) candidates.push(`clang-${major}`);
  candidates.push("cc");
  candidates.push(...visualStudioClangCandidates());
  return [...new Set(candidates)];
}

function tooOldError(probe: ClangProbe): ToolchainError {
  const version = probe.version!;
  const found = `clang ${version.major}.${version.minor}.${version.patch}`;
  return new ToolchainError(
    "clang --version",
    1,
    `${probe.command} reports ${found}, but xbintsc requires clang ${MINIMUM_CLANG_MAJOR} or newer ` +
      "(the emitted LLVM IR uses opaque pointers).\n" +
      "Install a newer LLVM/clang (on Windows, the one bundled with Visual Studio works) or set " +
      `xbintsc_CLANG to a clang ${MINIMUM_CLANG_MAJOR}+ binary.`,
  );
}

function noCompilerError(tried: readonly ClangProbe[]): ToolchainError {
  const responded = tried.filter((probe) => probe.responds);
  const details = responded.map((probe) => {
    const version = probe.version
      ? `clang ${probe.version.major}.${probe.version.minor}.${probe.version.patch}`
      : probe.summary || "unknown version";
    return `  ${probe.command}: ${version}`;
  });
  const tooOld = responded.some((probe) => probe.version !== undefined && probe.version.major < MINIMUM_CLANG_MAJOR);
  const reason = tooOld
    ? `Only clang older than ${MINIMUM_CLANG_MAJOR} was found; xbintsc requires clang ${MINIMUM_CLANG_MAJOR}+ ` +
      "(the emitted LLVM IR uses opaque pointers)."
    : `No clang ${MINIMUM_CLANG_MAJOR}+ was found.`;
  return new ToolchainError(
    "clang --version",
    1,
    [reason, ...(details.length > 0 ? ["Tested:", ...details] : []), "Set xbintsc_CLANG to a clang binary."].join("\n"),
  );
}

/**
 * Resolve the compiler to use, version-checked.
 *
 * An explicit `xbintsc_CLANG` / `xbintsc_TOOLCHAIN` wins even when it is older
 * than the minimum: the user asked for it, so the clear "too old" error is more
 * helpful than silently using another compiler. A broken explicit path falls
 * back to the system search. System candidates are probed in order and the
 * first one reporting clang >= {@link MINIMUM_CLANG_MAJOR} wins, so a modern
 * clang installed next to an old `/usr/bin/clang` is still found.
 */
export function locateClang(runner: Runner = realRunner): ClangLocation {
  const explicit = process.env.xbintsc_CLANG ?? process.env.xbintsc_TOOLCHAIN;
  const tried: ClangProbe[] = [];
  if (explicit) {
    const probe = probeClang(runner, explicit);
    if (probe.responds) {
      if (probe.version && probe.version.major < MINIMUM_CLANG_MAJOR) throw tooOldError(probe);
      return { command: explicit, source: "env", ...(probe.version ? { version: probe.version } : {}) };
    }
    tried.push(probe);
  }
  for (const command of systemClangCandidates()) {
    const probe = probeClang(runner, command);
    tried.push(probe);
    if (!probe.responds || !probe.version) continue;
    if (probe.version.major < MINIMUM_CLANG_MAJOR) continue;
    return { command, source: "system", version: probe.version };
  }
  throw noCompilerError(tried);
}

/** Locate a supported clang-compatible C compiler, or throw a clear error. */
export function findClang(runner: Runner = realRunner): string {
  return locateClang(runner).command;
}

export interface CompileIrOptions {
  readonly clang: string;
  readonly irPath: string;
  readonly objectPath: string;
  readonly optimize: string;
  readonly env?: Record<string, string>;
  /** Emit debug info (`-g`) and keep frame pointers for crash backtraces. */
  readonly debug?: boolean;
}

/**
 * One function/data item per section so the linker can drop everything a
 * program never references. This is what `--gc-sections` / `-dead_strip` need;
 * the runtime is a set of translation units with many unused entry points.
 */
const SECTION_FLAGS = ["-ffunction-sections", "-fdata-sections"];

/** Compile an LLVM IR file to a native object file. */
export function compileIr(runner: Runner, options: CompileIrOptions): void {
  const debugFlags = options.debug ? ["-g", "-fno-omit-frame-pointer"] : [];
  const result = runner.run(
    options.clang,
    [`-O${options.optimize}`, ...SECTION_FLAGS, ...debugFlags, "-c", options.irPath, "-o", options.objectPath],
    { env: options.env },
  );
  if (result.status !== 0) throw new ToolchainError(`${options.clang} ${options.irPath}`, result.status, result.stderr);
}

export interface LinkOptions {
  readonly clang: string;
  readonly objectPaths: readonly string[];
  readonly outputPath: string;
  readonly linkerFlags: readonly string[];
  readonly optimize: string;
  readonly env?: Record<string, string>;
}

/** Platform flag that drops unreferenced sections (paired with {@link SECTION_FLAGS}). */
function deadStripFlag(): string[] {
  if (process.platform === "win32") return []; // link.exe /OPT:REF is on by default
  if (process.platform === "darwin") return ["-Wl,-dead_strip"];
  return ["-Wl,--gc-sections"];
}

export function link(runner: Runner, options: LinkOptions): void {
  const args = [
    `-O${options.optimize}`,
    ...options.objectPaths,
    "-o",
    options.outputPath,
    ...deadStripFlag(),
    ...options.linkerFlags,
  ];
  const result = runner.run(options.clang, args, { env: options.env });
  if (result.status !== 0) throw new ToolchainError(`${options.clang} link`, result.status, result.stderr);
}

/**
 * Flags shared by every runtime/extension C compile. Exported so the object
 * cache key can change when they do: the cache is keyed by source content, and
 * a flag change is a different object.
 */
export const RUNTIME_CFLAGS = [
  "-O2",
  "-Wall",
  "-Wextra",
  "-D_CRT_SECURE_NO_WARNINGS",
  ...SECTION_FLAGS,
] as const;

/** Compile a C source to an object file (used for the runtime and extensions). */
export function compileC(
  runner: Runner,
  clang: string,
  sourcePath: string,
  objectPath: string,
  includeDir: string,
  env?: Record<string, string>,
): void {
  const result = runner.run(
    clang,
    [...RUNTIME_CFLAGS, "-c", sourcePath, "-o", objectPath, `-I${includeDir}`],
    { env },
  );
  if (result.status !== 0) throw new ToolchainError(`${clang} ${sourcePath}`, result.status, result.stderr);
}
