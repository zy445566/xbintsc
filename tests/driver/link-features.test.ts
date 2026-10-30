import { describe, expect, it } from "vitest";
import { DiagnosticBag, DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { createDefaultRegistry, type Extension, type ExtensionRegistry } from "../../src/extensions/registry.js";
import type { LinkFeature, ProgramUsage } from "../../src/extensions/link-feature.js";
import { resolveLinkFeatures } from "../../src/driver/link-features.js";

const EMPTY_USAGE: ProgramUsage = {
  strings: [],
  identifiers: new Set<string>(),
  getContext: [],
  dynamicMarkup: false,
};

/** A feature that is used when `needle` appears in a string literal. */
function feature(
  name: string,
  needle: string,
  options: { unavailable?: boolean; requires?: readonly string[] } = {},
): LinkFeature {
  return {
    name,
    description: `${name} feature`,
    runtimeSources: () => [`/runtime/${name}_bridge.c`],
    nativeObjects: options.unavailable
      ? () => {
          throw new Error(`${name} native library not found`);
        }
      : () => [`/runtime/${name}.a`],
    linkerFlags: () => (name === "media" ? ["-lm"] : []),
    ...(options.requires ? { requires: options.requires } : {}),
    used: (usage) => usage.strings.some((value) => value.includes(needle)),
  };
}

function registryWith(...features: readonly LinkFeature[]): ExtensionRegistry {
  const extension: Extension = { name: "fake", linkFeatures: () => features };
  return createDefaultRegistry().register(extension);
}

function resolve(
  registry: ExtensionRegistry,
  usage: ProgramUsage,
  request: { features?: readonly string[]; noFeatures?: readonly string[] } = {},
): { bag: DiagnosticBag; names: string[] } {
  const bag = new DiagnosticBag();
  const active = resolveLinkFeatures(registry, usage, request, bag);
  return { bag, names: active.map((entry) => entry.name) };
}

describe("resolveLinkFeatures", () => {
  it("activates a feature only when the program may use it", () => {
    const registry = registryWith(feature("media", "<video"));
    expect(resolve(registry, EMPTY_USAGE).names).toEqual([]);
    expect(resolve(registry, { ...EMPTY_USAGE, strings: ["<video>"] }).names).toEqual(["media"]);
  });

  it("forces features with --feature and keeps them out with --no-feature", () => {
    const registry = registryWith(feature("media", "<video"), feature("gpu", "webgl"));
    expect(resolve(registry, EMPTY_USAGE, { features: ["gpu"] }).names).toEqual(["gpu"]);
    expect(
      resolve(registry, { ...EMPTY_USAGE, strings: ["<video>", "webgl"] }, { noFeatures: ["media"] }).names,
    ).toEqual(["gpu"]);
  });

  it("expands all and lets a specific name override it", () => {
    const registry = registryWith(feature("media", "<video"), feature("gpu", "webgl"));
    expect(resolve(registry, EMPTY_USAGE, { features: ["all"] }).names).toEqual(["media", "gpu"]);
    expect(
      resolve(registry, EMPTY_USAGE, { features: ["all"], noFeatures: ["media"] }).names,
    ).toEqual(["gpu"]);
    expect(
      resolve(registry, EMPTY_USAGE, { features: ["media"], noFeatures: ["all"] }).names,
    ).toEqual(["media"]);
  });

  it("activates required features and rejects a blocked dependency", () => {
    const registry = registryWith(
      feature("gpu", "webgl"),
      feature("webgl", "webgl", { requires: ["gpu"] }),
    );
    expect(resolve(registry, EMPTY_USAGE, { features: ["webgl"] }).names).toEqual(["gpu", "webgl"]);
    const blocked = resolve(registry, EMPTY_USAGE, { features: ["webgl"], noFeatures: ["gpu"] });
    expect(blocked.bag.hasErrors).toBe(true);
    expect(blocked.bag.diagnostics[0]!.message).toContain("requires 'gpu'");
  });

  it("rejects contradictory or unknown names", () => {
    const registry = registryWith(feature("media", "<video"));
    expect(resolve(registry, EMPTY_USAGE, { features: ["media"], noFeatures: ["media"] }).bag.hasErrors).toBe(true);
    expect(resolve(registry, EMPTY_USAGE, { features: ["all"], noFeatures: ["all"] }).bag.hasErrors).toBe(true);
    const unknown = resolve(registry, EMPTY_USAGE, { features: ["nope"] });
    expect(unknown.bag.hasErrors).toBe(true);
    expect(unknown.bag.diagnostics[0]!.code).toBe(DiagnosticCode.LinkFeatureUnavailable);
    expect(unknown.bag.diagnostics[0]!.message).toContain("unknown link feature 'nope'");
  });

  it("does not expose a feature of an extension that is not enabled", () => {
    const empty = createDefaultRegistry();
    const result = resolve(empty, { ...EMPTY_USAGE, strings: ["<video>"] });
    expect(result.names).toEqual([]);
  });

  it("warns when an auto-detected feature is unavailable", () => {
    const registry = registryWith(feature("media", "<video", { unavailable: true }));
    const result = resolve(registry, { ...EMPTY_USAGE, strings: ["<video>"] });
    expect(result.names).toEqual([]);
    expect(result.bag.hasErrors).toBe(false);
    expect(result.bag.diagnostics).toHaveLength(1);
    expect(result.bag.diagnostics[0]!.category).toBe("warning");
    expect(result.bag.diagnostics[0]!.message).toContain("detected but is unavailable");
  });

  it("errors when a forced feature is unavailable", () => {
    const registry = registryWith(feature("media", "<video", { unavailable: true }));
    const result = resolve(registry, EMPTY_USAGE, { features: ["media"] });
    expect(result.bag.hasErrors).toBe(true);
    expect(result.bag.diagnostics[0]!.code).toBe(DiagnosticCode.LinkFeatureUnavailable);
    expect(result.bag.diagnostics[0]!.message).toContain("native library not found");
  });

  it("carries the bridge, archive and linker flags of an active feature", () => {
    const registry = registryWith(feature("media", "<video"));
    const bag = new DiagnosticBag();
    const active = resolveLinkFeatures(registry, { ...EMPTY_USAGE, strings: ["<video>"] }, {}, bag);
    expect(active).toEqual([
      {
        name: "media",
        description: "media feature",
        runtimeSources: ["/runtime/media_bridge.c"],
        nativeObjects: ["/runtime/media.a"],
        linkerFlags: ["-lm"],
      },
    ]);
  });
});
