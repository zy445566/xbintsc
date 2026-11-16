import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { dirname } from "node:path";
import {
  RUNTIME_STAMP_FILE,
  findRuntimeLibrary,
  freshRuntimeLibrary,
  platformRuntimeLibDirs,
  readRuntimeStamp,
  runtimeLibDir,
  runtimeLibDirs,
  runtimeSourceFingerprint,
  writeRuntimeStamp,
} from "../../src/driver/runtime-lib.js";
import { findRuntimeDir, platformSlug } from "../../src/driver/paths.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-runtime-lib-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("runtime libraries", () => {
  it("searches the package's per-platform folder first", () => {
    const dirs = runtimeLibDirs();
    expect(dirs[0]).toBe(join(findRuntimeDir(), "lib", platformSlug()));
    // `runtimeLibDir` reports whichever candidate actually holds archives (the
    // package itself in a checkout/release, the optional platform package in an
    // npm install).
    expect(dirs).toContain(runtimeLibDir());
  });

  it("returns undefined for an archive that is not bundled", () => {
    expect(findRuntimeLibrary("definitely-not-a-real-archive")).toBeUndefined();
  });

  it("finds a bundled core archive when present", () => {
    const core = findRuntimeLibrary("core");
    // Prebuilt archives are optional, but when present they must be absolute.
    if (core !== undefined) expect(core.endsWith(".a") || core.endsWith(".lib")).toBe(true);
  });

  it("prefers an installed platform package nested under or hoisted above xbintsc", () => {
    const root = temporaryDirectory();
    const packageRoot = join(root, "project", "node_modules", "xbintsc");
    const nested = join(packageRoot, "node_modules", "@xbintsc", "linux-x64", "runtime", "lib", "linux-x64");
    const hoisted = join(root, "project", "node_modules", "@xbintsc", "linux-x64", "runtime", "lib", "linux-x64");

    const dirs = platformRuntimeLibDirs(packageRoot, "linux-x64");
    expect(dirs).toContain(nested);
    expect(dirs).toContain(hoisted);
    expect(dirs.indexOf(nested)).toBeLessThan(dirs.indexOf(hoisted));

    mkdirSync(hoisted, { recursive: true });
    writeFileSync(join(hoisted, "core.a"), "archive");
    expect(findRuntimeLibrary("core", platformRuntimeLibDirs(packageRoot, "linux-x64"))).toBe(
      join(hoisted, "core.a"),
    );
  });

  it("falls back to the first candidate when no folder has archives", () => {
    const directory = temporaryDirectory();
    const missing = join(directory, "missing");
    expect(runtimeLibDir([missing])).toBe(missing);
    writeFileSync(join(directory, "core.a"), "archive");
    expect(runtimeLibDir([missing, directory])).toBe(directory);
    // A file passed where a directory is expected is simply not a library.
    expect(runtimeLibDir([join(directory, "core.a")])).toBe(join(directory, "core.a"));
  });

  it("probes the .lib flavour after .a in each folder", () => {
    const directory = temporaryDirectory();
    expect(findRuntimeLibrary("core", [directory])).toBeUndefined();
    writeFileSync(join(directory, "core.lib"), "archive");
    expect(findRuntimeLibrary("core", [directory])).toBe(join(directory, "core.lib"));
    writeFileSync(join(directory, "core.a"), "archive");
    expect(findRuntimeLibrary("core", [directory])).toBe(join(directory, "core.a"));
  });
});

describe("runtime archive freshness", () => {
  const FLAGS = ["-O2", "-std=c11"] as const;

  function writeFile(path: string, content: string): void {
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }

  it("changes the fingerprint when a runtime source changes", () => {
    const root = temporaryDirectory();
    const runtime = join(root, "runtime");
    writeFile(join(runtime, "xt_builtins.c"), "int a(void) { return 1; }");
    writeFile(join(runtime, "rt.h"), "#define A 1");
    const before = runtimeSourceFingerprint(runtime, "0.0.0", FLAGS);
    expect(runtimeSourceFingerprint(runtime, "0.0.0", FLAGS)).toBe(before);

    writeFile(join(runtime, "xt_builtins.c"), "int a(void) { return 2; }");
    expect(runtimeSourceFingerprint(runtime, "0.0.0", FLAGS)).not.toBe(before);
    // The compiler version and flags are part of the identity too.
    expect(runtimeSourceFingerprint(runtime, "0.0.0", ["-O0"])).not.toBe(
      runtimeSourceFingerprint(runtime, "0.0.0", FLAGS),
    );
    expect(runtimeSourceFingerprint(runtime, "9.9.9", FLAGS)).not.toBe(
      runtimeSourceFingerprint(runtime, "0.0.0", FLAGS),
    );
  });

  it("ignores the folder the archives and stamp live in", () => {
    const root = temporaryDirectory();
    const runtime = join(root, "runtime");
    writeFile(join(runtime, "xt_builtins.c"), "int a(void) { return 1; }");
    const before = runtimeSourceFingerprint(runtime, "0.0.0", FLAGS);
    writeFile(join(runtime, "lib", "win32-x64", "core.lib"), "not a source file");
    writeFile(join(runtime, "lib", "win32-x64", RUNTIME_STAMP_FILE), "{}");
    expect(runtimeSourceFingerprint(runtime, "0.0.0", FLAGS)).toBe(before);
  });

  it("rejects an archive whose stamp is missing or stale", () => {
    const root = temporaryDirectory();
    const runtime = join(root, "runtime");
    writeFile(join(runtime, "xt_builtins.c"), "int a(void) { return 1; }");
    const libDir = join(runtime, "lib", "test");
    writeFile(join(libDir, "core.a"), "archive bytes");
    const fingerprint = runtimeSourceFingerprint(runtime, "0.0.0", FLAGS);

    // No stamp at all: treat as stale (a release always writes one).
    expect(freshRuntimeLibrary("core", [libDir], fingerprint).stale).toBe(join(libDir, "core.a"));

    writeRuntimeStamp(libDir, fingerprint, "0.0.0");
    expect(readRuntimeStamp(libDir)).toBe(fingerprint);
    expect(freshRuntimeLibrary("core", [libDir], fingerprint).path).toBe(join(libDir, "core.a"));

    // A changed source invalidates the archive recorded next to it.
    writeFile(join(runtime, "xt_builtins.c"), "int a(void) { return 2; }");
    const changed = runtimeSourceFingerprint(runtime, "0.0.0", FLAGS);
    expect(freshRuntimeLibrary("core", [libDir], changed).stale).toBe(join(libDir, "core.a"));
    expect(freshRuntimeLibrary("core", [libDir], changed).path).toBeUndefined();
  });
});