import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { buildCacheKey, type CacheKeyInput } from "../../src/driver/build-key.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";

const directories: string[] = [];

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

/** A minimal key input; `runtimeDir` points at an empty temp tree so the
 *  runtime fingerprint is stable and the test never reads the checkout. */
function input(overrides: Partial<CacheKeyInput> = {}): CacheKeyInput {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-build-key-"));
  directories.push(directory);
  return {
    sourceText: "console.log(1);",
    emit: "exe",
    optimize: "2",
    arch: "x64",
    registry: createDefaultRegistry(),
    runtimeDir: join(directory, "runtime"),
    app: {},
    storage: { backend: "memory", path: "" },
    debug: false,
    lto: false,
    check: true,
    pgoDir: join(directory, "pgo"),
    toolchain: { clang: "clang", linkerArgs: [] },
    ...overrides,
  };
}

describe("buildCacheKey inputs", () => {
  it("includes the Windows console-subsystem choice", () => {
    expect(buildCacheKey(input({ app: { console: true } }))).not.toBe(
      buildCacheKey(input({ app: { console: false } })),
    );
  });

  it("includes the host architecture", () => {
    expect(buildCacheKey(input({ arch: "x64" }))).not.toBe(buildCacheKey(input({ arch: "arm64" })));
  });

  it("includes the resolved toolchain, SDK and linker flags", () => {
    const base = buildCacheKey(input());
    expect(buildCacheKey(input({ toolchain: { clang: "clang", linkerArgs: [] } }))).toBe(base);
    expect(
      buildCacheKey(input({ toolchain: { clang: "/opt/llvm/bin/clang", linkerArgs: [] } })),
    ).not.toBe(base);
    expect(
      buildCacheKey(input({ toolchain: { clang: "clang", sdkRoot: "/SDKs/MacOSX.sdk", linkerArgs: [] } })),
    ).not.toBe(base);
    expect(
      buildCacheKey(input({ toolchain: { clang: "clang", linkerArgs: ["-fuse-ld=lld"] } })),
    ).not.toBe(base);
  });
});
