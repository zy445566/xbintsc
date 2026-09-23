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
    const registry = createDefaultRegistry().register(demoExtension);
    const builtins = registry.builtins();
    expect(builtins.print?.symbol).toBe("xt_println");
    expect(builtins.demo?.symbol).toBe("xt_demo");
  });

  it("exposes importable modules from every extension", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const modules = registry.modules();
    expect(modules.fs?.exports?.readFileSync?.symbol).toBe("xt_node_read_text_file");
    expect(modules["node:fs"]?.exports?.writeFileSync?.symbol).toBe("xt_node_write_file");
    expect(modules["fs/promises"]?.exports?.readFile?.symbol).toBe("xt_node_p_read_file");
    expect(modules.path?.namespace).toBe("path");
    expect(modules["node:path"]?.exports?.join?.namespace).toBe("path");
    expect(modules.events?.exports?.EventEmitter?.isConstructor).toBe(true);
    expect(modules.events?.exports?.once?.namespace).toBe("events");
    expect(modules.util?.exports?.format?.symbol).toBe("xt_util_format");
    expect(modules.querystring?.exports?.parse?.symbol).toBe("xt_querystring_parse");
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

  it("hints unregistered extensions by their module specifiers", () => {
    const registry = createDefaultRegistry().hintExtension(nodeExtension);
    const hints = registry.moduleHints();
    expect(hints["fs"]).toBe("node");
    expect(hints["node:http"]).toBe("node");
  });

  it("drops hints once the extension is registered", () => {
    const registry = createDefaultRegistry().hintExtension(nodeExtension).register(nodeExtension);
    expect(registry.moduleHints()["fs"]).toBeUndefined();
    expect(registry.moduleHints()["node:http"]).toBeUndefined();
  });

  it("node extension points at the ext C sources", () => {
    const sources = nodeExtension.runtimeSources?.() ?? [];
    expect(sources.length).toBeGreaterThanOrEqual(6);
    expect(sources.some((s) => /ext_node[\\/]fs[\\/]read_file\.c$/.test(s))).toBe(true);
    expect(sources.some((s) => /ext_node[\\/]fs[\\/]fs_ops\.c$/.test(s))).toBe(true);
    expect(sources.some((s) => /ext_node[\\/]path[\\/]path\.c$/.test(s))).toBe(true);
    expect(sources.some((s) => /ext_node[\\/]os[\\/]os\.c$/.test(s))).toBe(true);
    expect(sources.some((s) => /ext_node[\\/]process[\\/]process\.c$/.test(s))).toBe(true);
  });
});
