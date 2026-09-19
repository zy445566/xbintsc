import { describe, expect, it } from "vitest";
import { ExtensionRegistry, createDefaultRegistry, coreExtension, type Extension } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

const demoExtension: Extension = {
  name: "demo",
  builtins: () => ({ demo: { symbol: "xt_demo" }, ping: { symbol: "xt_ping", returnVoid: true } }),
  runtimeSources: () => ["/tmp/demo.c"],
  linkerFlags: () => ["-ldemo"],
};

describe("ExtensionRegistry", () => {
  it("registers and retrieves extensions", () => {
    const registry = new ExtensionRegistry().register(demoExtension);
    expect(registry.has("demo")).toBe(true);
    expect(registry.get("demo")).toBe(demoExtension);
    expect(registry.all()).toHaveLength(1);
  });

  it("rejects duplicate names", () => {
    const registry = new ExtensionRegistry().register(demoExtension);
    expect(() => registry.register(demoExtension)).toThrow(/already registered/);
  });

  it("unregisters extensions", () => {
    const registry = new ExtensionRegistry().register(demoExtension);
    expect(registry.unregister("demo")).toBe(true);
    expect(registry.unregister("demo")).toBe(false);
  });

  it("merges builtins from every extension", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const builtins = registry.builtins();
    expect(builtins.print?.symbol).toBe("xt_println");
    expect(builtins.readFileSync?.symbol).toBe("xt_node_read_text_file");
  });

  it("collects runtime sources and linker flags", () => {
    const registry = new ExtensionRegistry().register(demoExtension);
    expect(registry.runtimeSources()).toEqual(["/tmp/demo.c"]);
    expect(registry.linkerFlags()).toEqual(["-ldemo"]);
  });

  it("ships a core extension exposing print", () => {
    expect(coreExtension.builtins?.().print?.symbol).toBe("xt_println");
    expect(createDefaultRegistry().has("core")).toBe(true);
  });

  it("node extension points at the ext C source", () => {
    const sources = nodeExtension.runtimeSources?.() ?? [];
    expect(sources).toHaveLength(1);
    expect(sources[0]).toMatch(/ext_node[\\/]fs[\\/]read_file\.c$/);
  });
});
