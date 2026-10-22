/**
 * Windows PE icon resource.
 *
 * Windows reads the application icon from a resource section in the `.exe`, so
 * an embedded blob is not enough: the icon is wrapped in a `.rc` script and
 * compiled to a resource object that is linked into the executable. `llvm-rc`
 * (shipping with clang) is preferred; MinGW `windres` is the fallback. Both are
 * optional — a missing resource compiler degrades to the runtime window icon
 * instead of failing the build.
 */

import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { hashParts } from "./cache.js";
import { IconError, toIcoBytes, type IconInfo } from "./icon.js";
import type { Runner } from "./toolchain.js";

export type ResourceCompilerKind = "llvm-rc" | "windres";

export interface ResourceCompiler {
  readonly tool: string;
  readonly kind: ResourceCompilerKind;
}

/**
 * Probe a resource compiler.
 *
 * `llvm-rc` has no `--version` flag: any invocation that is not a valid
 * conversion exits non-zero, but a usage-only run (`/?`) prints the tool name,
 * which is what identifies it. Probing `llvm-rc --version` therefore always
 * failed and silently selected the MinGW `windres` fallback — and a 32-bit
 * `windres` emits an i386 resource object that the x64 linker rejects with
 * `LNK1112`. Treat a usage banner as success so the compiler that matches the
 * clang ABI wins.
 */
function isResourceCompiler(runner: Runner, candidate: ResourceCompiler): boolean {
  const args = candidate.kind === "llvm-rc" ? ["/?"] : ["--version"];
  try {
    const result = runner.run(candidate.tool, args);
    if (result.status === 0) return true;
    return candidate.kind === "llvm-rc" && /LLVM Resource Converter/i.test(result.stdout + result.stderr);
  } catch {
    return false;
  }
}

/**
 * Locate a resource compiler. `xbintsc_RC` wins, then `llvm-rc` next to the
 * resolved clang, then `llvm-rc` and `windres` on `PATH`.
 */
export function resolveResourceCompiler(runner: Runner, clang: string): ResourceCompiler | undefined {
  const exe = process.platform === "win32" ? ".exe" : "";
  // An explicit override is trusted: probe it and fall back rather than
  // rejecting it on a path prefix check (it may be a bare command on PATH).
  const override = process.env.xbintsc_RC;
  if (override) {
    const kind: ResourceCompilerKind = /windres/i.test(override) ? "windres" : "llvm-rc";
    if (isResourceCompiler(runner, { tool: override, kind })) return { tool: override, kind };
  }
  const candidates: ResourceCompiler[] = [];
  if (clang && (clang.includes("/") || clang.includes("\\"))) {
    candidates.push({ tool: join(dirname(clang), `llvm-rc${exe}`), kind: "llvm-rc" });
  }
  candidates.push({ tool: `llvm-rc${exe}`, kind: "llvm-rc" });
  candidates.push({ tool: `windres${exe}`, kind: "windres" });

  for (const candidate of candidates) {
    if ((candidate.tool.includes("/") || candidate.tool.includes("\\")) && !existsSync(candidate.tool)) continue;
    if (isResourceCompiler(runner, candidate)) return candidate;
  }
  return undefined;
}

/**
 * The COFF/COFF-PE target `windres` must emit. Without an explicit `-F` it
 * defaults to `pe-i386` even when the tool itself is 64-bit hosted, and the
 * x64 clang linker then fails with `LNK1112: module machine type 'x86'
 * conflicts with target machine type 'x64'`.
 */
function windresTargetFormat(): string {
  return process.arch === "arm64" ? "pe-aarch64" : "pe-x86-64";
}

/**
 * Compile the icon into a linkable PE resource object (`.res` for llvm-rc, a
 * COFF `.o` for windres). Cached by contents and compiler kind.
 */
export function ensureWindowsIconResource(
  runner: Runner,
  cacheDir: string,
  icon: IconInfo,
  compiler: ResourceCompiler,
  env?: Record<string, string>,
): string {
  const icoBytes = toIcoBytes(icon);
  const key = hashParts(["icon-res", compiler.kind, Buffer.from(icoBytes).toString("base64")]);
  const icoName = `icon-${key}.ico`;
  const icoPath = join(cacheDir, icoName);
  const rcPath = join(cacheDir, `icon-${key}.rc`);
  const objectPath = join(cacheDir, `icon-${key}.${compiler.kind === "windres" ? "o" : "res"}`);
  if (existsSync(objectPath)) return objectPath;

  mkdirSync(cacheDir, { recursive: true });
  writeFileSync(icoPath, icoBytes);
  // The .rc references the .ico by name; the compiler runs with `cacheDir` as
  // its working directory so the relative path resolves.
  writeFileSync(rcPath, `1 ICON "${icoName}"\n`);

  const args =
    compiler.kind === "windres"
      ? [rcPath, "-O", "coff", "-F", windresTargetFormat(), "-o", objectPath]
      : ["/fo", objectPath, rcPath];
  const result = runner.run(compiler.tool, args, { cwd: cacheDir, env });
  if (result.status !== 0 || !existsSync(objectPath)) {
    throw new IconError(
      `Failed to compile the Windows icon resource with ${compiler.tool}:\n${result.stderr || result.stdout}`,
    );
  }
  return objectPath;
}
