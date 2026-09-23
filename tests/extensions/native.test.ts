import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import {
  NativeExtensionError,
  loadNativeManifest,
  nativeExtensionFromManifest,
  parseNativeManifest,
} from "../../src/extensions/native.js";
import { ExtensionRegistry, createDefaultRegistry } from "../../src/extensions/registry.js";

describe("parseNativeManifest", () => {
  it("normalises a full manifest", () => {
    const manifest = parseNativeManifest({
      name: "mathx",
      description: "C++ helpers",
      objects: ["build/libmathx.a"],
      linkerFlags: ["-lm"],
      linkerFlagsByPlatform: { linux: ["-lstdc++"], darwin: ["-lc++"] },
      builtins: { fastAdd: { symbol: "mathx_add" } },
      modules: {
        mathx: {
          exports: {
            add: { symbol: "mathx_add" },
            join: { namespace: "path", method: "join" },
            Thing: { symbol: "xt_thing_ctor", isConstructor: true },
          },
        },
      },
    });
    expect(manifest.name).toBe("mathx");
    expect(manifest.objects).toEqual(["build/libmathx.a"]);
    expect(manifest.linkerFlagsByPlatform?.darwin).toEqual(["-lc++"]);
    expect(manifest.builtins?.fastAdd?.symbol).toBe("mathx_add");
    expect(manifest.modules?.mathx?.exports?.join).toEqual({ namespace: "path", method: "join" });
    expect(manifest.modules?.mathx?.exports?.Thing?.isConstructor).toBe(true);
  });

  it("defaults omitted collections to empty", () => {
    const manifest = parseNativeManifest({ name: "bare" });
    expect(manifest.objects).toEqual([]);
    expect(manifest.linkerFlags).toEqual([]);
    expect(manifest.builtins).toBeUndefined();
    expect(manifest.modules).toBeUndefined();
  });

  it("rejects a missing or empty name", () => {
    expect(() => parseNativeManifest({})).toThrow(NativeExtensionError);
    expect(() => parseNativeManifest({ name: "   " })).toThrow(/non-empty 'name'/);
  });

  it("rejects a builtin without a symbol", () => {
    expect(() => parseNativeManifest({ name: "x", builtins: { f: {} } })).toThrow(/'symbol'/);
  });

  it("rejects an export with neither symbol nor namespace", () => {
    expect(() => parseNativeManifest({ name: "x", modules: { m: { exports: { f: {} } } } })).toThrow(
      /needs a 'symbol' or a 'namespace'/,
    );
  });

  it("rejects non-string linker flags", () => {
    expect(() => parseNativeManifest({ name: "x", linkerFlags: [42] })).toThrow(/array of strings/);
  });
});

describe("nativeExtensionFromManifest", () => {
  let workdir: string;
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-native-"));
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function writeManifest(name: string, contents: unknown): string {
    const path = join(workdir, name);
    writeFileSync(path, JSON.stringify(contents));
    return path;
  }

  it("resolves objects relative to the manifest and verifies they exist", () => {
    writeFileSync(join(workdir, "libmathx.a"), "fake archive");
    const manifestPath = writeManifest("mathx.manifest.json", {
      name: "mathx",
      objects: ["libmathx.a"],
      linkerFlagsByPlatform: { [process.platform]: ["-lc++"] },
      builtins: { fastAdd: { symbol: "mathx_add" } },
    });
    const extension = nativeExtensionFromManifest(manifestPath);
    expect(extension.name).toBe("mathx");
    expect(extension.nativeObjects!()).toEqual([resolve(workdir, "libmathx.a")]);
    expect(extension.linkerFlags!()).toEqual(["-lc++"]);
    expect(extension.builtins!().fastAdd?.symbol).toBe("mathx_add");
  });

  it("throws a helpful error for a missing object", () => {
    const manifestPath = writeManifest("missing.manifest.json", {
      name: "missing",
      objects: ["libnope.a"],
    });
    expect(() => nativeExtensionFromManifest(manifestPath)).toThrow(/references missing object/);
  });

  it("reports invalid JSON", () => {
    const path = join(workdir, "broken.json");
    writeFileSync(path, "{ not json");
    expect(() => loadNativeManifest(path)).toThrow(/not valid JSON/);
  });

  it("flattens native objects through the registry", () => {
    writeFileSync(join(workdir, "libdemo.a"), "fake");
    const manifestPath = writeManifest("demo.manifest.json", { name: "demo", objects: ["libdemo.a"] });
    const registry = createDefaultRegistry().register(nativeExtensionFromManifest(manifestPath));
    expect(registry.nativeObjects()).toEqual([resolve(workdir, "libdemo.a")]);
    expect(new ExtensionRegistry().nativeObjects()).toEqual([]);
  });
});
