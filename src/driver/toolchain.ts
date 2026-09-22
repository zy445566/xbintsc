/**
 * Thin wrappers around the external toolchain (clang) and process execution.
 *
 * Everything that shells out lives here so the rest of the driver stays
 * testable: unit tests can pass a fake `Runner` and never touch the host.
 */

import { spawnSync, type SpawnSyncOptions } from "node:child_process";

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

/** Locate a clang-compatible C compiler, honouring `xbintsc_CLANG`. */
export function findClang(runner: Runner = realRunner): string {
  const candidates = [
    process.env.xbintsc_CLANG,
    "clang",
    "clang-18",
    "clang-17",
    "clang-16",
    "cc",
  ].filter((candidate): candidate is string => Boolean(candidate));
  for (const candidate of candidates) {
    const result = runner.run(candidate, ["--version"]);
    if (result.status === 0) return candidate;
  }
  throw new ToolchainError("clang --version", 1, "No C compiler found. Set xbintsc_CLANG to a clang binary.");
}

export interface CompileIrOptions {
  readonly clang: string;
  readonly irPath: string;
  readonly objectPath: string;
  readonly optimize: string;
  readonly env?: Record<string, string>;
}

/** Compile an LLVM IR file to a native object file. */
export function compileIr(runner: Runner, options: CompileIrOptions): void {
  const result = runner.run(
    options.clang,
    [`-O${options.optimize}`, "-c", options.irPath, "-o", options.objectPath],
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

export function link(runner: Runner, options: LinkOptions): void {
  const args = [`-O${options.optimize}`, ...options.objectPaths, "-o", options.outputPath, ...options.linkerFlags];
  const result = runner.run(options.clang, args, { env: options.env });
  if (result.status !== 0) throw new ToolchainError(`${options.clang} link`, result.status, result.stderr);
}

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
    ["-O2", "-Wall", "-Wextra", "-D_CRT_SECURE_NO_WARNINGS", "-c", sourcePath, "-o", objectPath, `-I${includeDir}`],
    { env },
  );
  if (result.status !== 0) throw new ToolchainError(`${clang} ${sourcePath}`, result.status, result.stderr);
}
