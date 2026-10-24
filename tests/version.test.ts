import { describe, expect, it } from "vitest";
import { COMPILER_VERSION } from "../src/driver/compiler.js";
import { readTextFile } from "./platform.js";

const packageJson = JSON.parse(readTextFile(new URL("../package.json", import.meta.url))) as {
  version: string;
};

describe("compiler version", () => {
  it("matches package.json", () => {
    expect(COMPILER_VERSION).toBe(packageJson.version);
  });

  it("is part of the incremental cache key", () => {
    const source = readTextFile(new URL("../src/driver/build-key.ts", import.meta.url));
    expect(source).toMatch(/hashParts\(\[\s*COMPILER_VERSION,/);
  });

  it("keeps the version declaration out of compiler.ts", () => {
    const source = readTextFile(new URL("../src/driver/compiler.ts", import.meta.url));
    expect(source).not.toMatch(/export const COMPILER_VERSION =/);
  });

  it("is printed by `xbintsc version`", async () => {
    const { run } = await import("../src/cli/main.js");
    const lines: string[] = [];
    const code = run(["version"], {
      stdout: (text) => lines.push(text),
      stderr: (text) => lines.push(text),
    });
    expect(code).toBe(0);
    expect(lines.join("")).toContain(COMPILER_VERSION);
  });
});
