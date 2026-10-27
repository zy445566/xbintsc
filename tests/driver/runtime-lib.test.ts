import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  findRuntimeLibrary,
  platformRuntimeLibDirs,
  runtimeLibDir,
  runtimeLibDirs,
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
