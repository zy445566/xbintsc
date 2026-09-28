import { describe, expect, it } from "vitest";
import * as api from "../src/index.js";

describe("public API surface", () => {
  it("re-exports the compiler pipeline", () => {
    expect(typeof api.Scanner).toBe("function");
    expect(typeof api.Parser).toBe("function");
    expect(typeof api.bind).toBe("function");
    expect(typeof api.generate).toBe("function");
    expect(typeof api.hashText).toBe("function");
    expect(api.TokenKind).toBeDefined();
    expect(api.SymbolKind).toBeDefined();
    expect(api.XT_UNDEFINED).toBeDefined();
    expect(typeof api.numberLiteral).toBe("function");
    expect(typeof api.booleanLiteral).toBe("function");
  });

  it("re-exports the extension system and driver", () => {
    expect(typeof api.ExtensionRegistry).toBe("function");
    expect(typeof api.createDefaultRegistry).toBe("function");
    expect(api.coreExtension.name).toBe("core");
    expect(api.nodeExtension.name).toBe("node");
    expect(typeof api.bundledExtensions).toBe("function");
    expect(api.bundledExtensions().length).toBeGreaterThan(0);
    expect(typeof api.build).toBe("function");
    expect(typeof api.compileString).toBe("function");
    expect(typeof api.BuildCache).toBe("function");
    expect(typeof api.findRuntimeDir).toBe("function");
    expect(typeof api.findRuntimeLibrary).toBe("function");
    expect(typeof api.resolveToolchain).toBe("function");
  });
});
