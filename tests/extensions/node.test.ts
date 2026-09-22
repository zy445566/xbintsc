import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { describe, expect, it } from "vitest";
import { resolveFrom } from "../../src/extensions/node/module.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { findRuntimeDir } from "../../src/driver/paths.js";

describe("resolveFrom", () => {
  it("anchors runtime-relative sources to the discovered runtime directory", () => {
    const result = resolveFrom(import.meta.url, "../../runtime/ext_node/fs/read_file.c");
    expect(result).toBe(join(findRuntimeDir(), "ext_node/fs/read_file.c"));
  });

  it("falls back to the importing module's own directory", () => {
    const url = pathToFileURL(join(findRuntimeDir(), "fake", "module.js")).href;
    const result = resolveFrom(url, "./helper.c");
    expect(result).toBe(resolve(dirname(fileURLToPath(url)), "./helper.c"));
  });
});

describe("nodeExtension", () => {
  it("exposes every module under both bare and node: names", () => {
    const modules = nodeExtension.modules!();
    expect(modules.fs).toBeDefined();
    expect(modules["node:fs"]).toBe(modules.fs);
    expect(modules.path).toBeDefined();
    expect(modules["node:path"]).toBe(modules.path);
  });

  it("deduplicates the runtime sources it contributes", () => {
    const sources = nodeExtension.runtimeSources!();
    expect(sources.length).toBeGreaterThan(0);
    expect(new Set(sources).size).toBe(sources.length);
  });
});
