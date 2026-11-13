import { describe, expect, it } from "vitest";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { resolveLinkFeatures } from "../../src/driver/link-features.js";
import { collectUsage } from "../../src/driver/usage.js";
import { createDefaultRegistry, coreExtension } from "../../src/extensions/registry.js";
import { hasWasmUsage, wasmFeature } from "../../src/extensions/wasm.js";
import { loadWasmAsset } from "../../src/extensions/wasm-asset.js";
import { parse } from "../helpers.js";

describe("core extension wasm link feature", () => {
  it("declares the wasm feature with the wasm3 and bridge sources", () => {
    const features = coreExtension.linkFeatures?.() ?? [];
    const wasm = features.find((feature) => feature.name === "wasm");
    expect(wasm).toBe(wasmFeature);
    expect(wasm?.required).toBe(true);
    const sources = wasm?.runtimeSources?.() ?? [];
    expect(sources.some((source) => /wasm3[\\/]m3_env\.c$/.test(source))).toBe(true);
    expect(sources.some((source) => /xt_wasm[\\/]wasm_instance\.c$/.test(source))).toBe(true);
    expect(sources.some((source) => /xt_wasm[\\/]wasm_bridge\.c$/.test(source))).toBe(true);
  });
});

describe("hasWasmUsage", () => {
  const usage = (text: string): ReturnType<typeof collectUsage> =>
    collectUsage(parse(text).file);

  it("detects direct and aliased uses of the namespace", () => {
    expect(hasWasmUsage(usage("WebAssembly.instantiate(bytes);"))).toBe(true);
    expect(hasWasmUsage(usage("const W = WebAssembly; W.validate(bytes);"))).toBe(true);
  });

  it("does not fire for a program that never touches WebAssembly", () => {
    expect(hasWasmUsage(usage("console.log('hello');"))).toBe(false);
    expect(hasWasmUsage(usage("const wasm = 1; console.log(wasm);"))).toBe(false);
  });

  it("is selected by resolveLinkFeatures only for a wasm-using program", () => {
    const registry = createDefaultRegistry();
    const bag = new DiagnosticBag();

    const using = resolveLinkFeatures(registry, usage("WebAssembly.validate(bytes);"), {}, bag);
    expect(using.map((feature) => feature.name)).toContain("wasm");

    const unused = resolveLinkFeatures(registry, usage("console.log(1);"), {}, bag);
    expect(unused.map((feature) => feature.name)).not.toContain("wasm");
    expect(bag.diagnostics).toEqual([]);
  });
});

describe("wasm asset loader", () => {
  it("is the core extension's binary .wasm loader", () => {
    const loaders = coreExtension.assetLoaders?.() ?? {};
    expect(loaders[".wasm"]).toBe(loadWasmAsset);
    expect(loadWasmAsset.binary).toBe(true);
  });

  it("embeds the bytes as a base64-decoded Uint8Array module", () => {
    const loaded = loadWasmAsset("add.wasm", "\0asm\u0001\0\0\0");
    expect(loaded.moduleSource).toContain('atob("AGFzbQEAAAA=")');
    expect(loaded.moduleSource).toContain("export default __xt_wasm_bytes;");
  });
});
