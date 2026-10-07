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
    try {
      if (runner.run(override, ["--version"]).status === 0) return { tool: override, kind };
    } catch {
      // Fall through to the discovered compilers.
    }
  }
  const candidates: ResourceCompiler[] = [];
  if (clang && (clang.includes("/") || clang.includes("\\"))) {
    candidates.push({ tool: join(dirname(clang), `llvm-rc${exe}`), kind: "llvm-rc" });
  }
  candidates.push({ tool: `llvm-rc${exe}`, kind: "llvm-rc" });
  candidates.push({ tool: `windres${exe}`, kind: "windres" });

  for (const candidate of candidates) {
    if ((candidate.tool.includes("/") || candidate.tool.includes("\\")) && !existsSync(candidate.tool)) continue;
    try {
      if (runner.run(candidate.tool, ["--version"]).status === 0) return candidate;
    } catch {
      // Not runnable; try the next candidate.
    }
  }
  return undefined;
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
      ? [rcPath, "-O", "coff", "-o", objectPath]
      : ["/fo", objectPath, rcPath];
  const result = runner.run(compiler.tool, args, { cwd: cacheDir, env });
  if (result.status !== 0 || !existsSync(objectPath)) {
    throw new IconError(
      `Failed to compile the Windows icon resource with ${compiler.tool}:\n${result.stderr || result.stdout}`,
    );
  }
  return objectPath;
}
