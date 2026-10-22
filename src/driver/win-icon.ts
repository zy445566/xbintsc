/**
 * Windows PE icon resource.
 *
 * Windows reads the application icon from a resource section in the `.exe`, so
 * an embedded blob is not enough: the icon is wrapped in a `.rc` script and
 * compiled to a resource object that is linked into the executable. `llvm-rc`
 * (shipping with LLVM/clang, and next to the clang xbintsc resolves) produces
 * the MSVC-compatible `.res` object the linker expects.
 */

import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { hashParts } from "./cache.js";
import { IconError, toIcoBytes, type IconInfo } from "./icon.js";
import type { Runner } from "./toolchain.js";

export interface ResourceCompiler {
  readonly tool: string;
}

/**
 * Probe `llvm-rc`.
 *
 * It has no `--version` flag: any invocation that is not a valid conversion
 * exits non-zero, but a usage-only run (`/?`) prints the tool name, which is
 * what identifies it. Treat a usage banner as success so a real `llvm-rc`
 * installed next to clang is not skipped.
 */
function isLlvmRc(runner: Runner, tool: string): boolean {
  try {
    const result = runner.run(tool, ["/?"]);
    if (result.status === 0) return true;
    return /LLVM Resource Converter/i.test(result.stdout + result.stderr);
  } catch {
    return false;
  }
}

/**
 * Locate `llvm-rc`. `xbintsc_RC` wins, then `llvm-rc` next to the resolved
 * clang, then `llvm-rc` on `PATH`. Returns `undefined` when none exists; the
 * caller then builds without the embedded icon.
 */
export function resolveResourceCompiler(runner: Runner, clang: string): ResourceCompiler | undefined {
  const exe = process.platform === "win32" ? ".exe" : "";
  // An explicit override is trusted: probe it and fall back rather than
  // rejecting it on a path prefix check (it may be a bare command on PATH).
  const override = process.env.xbintsc_RC;
  if (override && isLlvmRc(runner, override)) return { tool: override };

  const candidates: string[] = [];
  if (clang && (clang.includes("/") || clang.includes("\\"))) {
    candidates.push(join(dirname(clang), `llvm-rc${exe}`));
  }
  candidates.push(`llvm-rc${exe}`);

  for (const tool of candidates) {
    if ((tool.includes("/") || tool.includes("\\")) && !existsSync(tool)) continue;
    if (isLlvmRc(runner, tool)) return { tool };
  }
  return undefined;
}

/**
 * Compile the icon into a linkable PE resource object (`.res`, cached by
 * contents).
 */
export function ensureWindowsIconResource(
  runner: Runner,
  cacheDir: string,
  icon: IconInfo,
  compiler: ResourceCompiler,
  env?: Record<string, string>,
): string {
  const icoBytes = toIcoBytes(icon);
  const key = hashParts(["icon-res", Buffer.from(icoBytes).toString("base64")]);
  const icoName = `icon-${key}.ico`;
  const icoPath = join(cacheDir, icoName);
  const rcPath = join(cacheDir, `icon-${key}.rc`);
  const objectPath = join(cacheDir, `icon-${key}.res`);
  if (existsSync(objectPath)) return objectPath;

  mkdirSync(cacheDir, { recursive: true });
  writeFileSync(icoPath, icoBytes);
  // The .rc references the .ico by name; the compiler runs with `cacheDir` as
  // its working directory so the relative path resolves.
  writeFileSync(rcPath, `1 ICON "${icoName}"\n`);

  const result = runner.run(compiler.tool, ["/fo", objectPath, rcPath], { cwd: cacheDir, env });
  if (result.status !== 0 || !existsSync(objectPath)) {
    throw new IconError(
      `Failed to compile the Windows icon resource with ${compiler.tool}:\n${result.stderr || result.stdout}`,
    );
  }
  return objectPath;
}
