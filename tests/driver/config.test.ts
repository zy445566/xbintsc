import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  CONFIG_FILE_NAME,
  findProjectConfig,
  loadProjectConfig,
  parseProjectConfig,
  ProjectConfigError,
  resolveConfigPaths,
} from "../../src/driver/config.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-config-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("parseProjectConfig", () => {
  it("accepts a full config", () => {
    const config = parseProjectConfig(
      JSON.stringify({
        $schema: "https://example.com/schema.json",
        entry: "src/app.ts",
        outDir: "build",
        output: "build/app",
        optimize: "3",
        extensions: ["gui", "node"],
        extNative: ["native/xbintsc.manifest.json"],
        force: true,
        app: { name: "Demo", icon: "assets/app.png", bundle: true, bundleId: "com.example.demo" },
      }),
    );
    expect(config).toEqual({
      entry: "src/app.ts",
      outDir: "build",
      output: "build/app",
      optimize: "3",
      extensions: ["gui", "node"],
      extNative: ["native/xbintsc.manifest.json"],
      force: true,
      app: { name: "Demo", icon: "assets/app.png", bundle: true, bundleId: "com.example.demo" },
    });
  });

  it("drops unknown keys and empty app fields", () => {
    const config = parseProjectConfig(JSON.stringify({ entry: "a.ts", $schema: "x", app: {} }));
    expect(config).toEqual({ entry: "a.ts", app: {} });
  });

  it("rejects invalid JSON", () => {
    expect(() => parseProjectConfig("{ not json")).toThrow(ProjectConfigError);
  });

  it("rejects a non-object root", () => {
    expect(() => parseProjectConfig("[]")).toThrow(/must be a JSON object/);
  });

  it("rejects wrongly typed fields", () => {
    expect(() => parseProjectConfig(JSON.stringify({ entry: 1 }))).toThrow(/must be a string/);
    expect(() => parseProjectConfig(JSON.stringify({ force: "yes" }))).toThrow(/must be a boolean/);
    expect(() => parseProjectConfig(JSON.stringify({ extensions: [1] }))).toThrow(/array of strings/);
    expect(() => parseProjectConfig(JSON.stringify({ app: [] }))).toThrow(/must be a JSON object/);
    expect(() => parseProjectConfig(JSON.stringify({ app: { name: 1 } }))).toThrow(/must be a string/);
  });

  it("rejects an unknown optimization level", () => {
    expect(() => parseProjectConfig(JSON.stringify({ optimize: "4" }))).toThrow(/optimize/);
  });
});

describe("loadProjectConfig", () => {
  it("reports a missing file", () => {
    expect(() => loadProjectConfig(join(temporaryDirectory(), CONFIG_FILE_NAME))).toThrow(
      /not found/,
    );
  });

  it("exposes the resolved path and directory", () => {
    const directory = temporaryDirectory();
    writeFileSync(join(directory, CONFIG_FILE_NAME), JSON.stringify({ entry: "src/app.ts" }));
    const loaded = loadProjectConfig(join(directory, CONFIG_FILE_NAME));
    expect(loaded.path).toBe(join(directory, CONFIG_FILE_NAME));
    expect(loaded.directory).toBe(directory);
    expect(loaded.config.entry).toBe("src/app.ts");
  });
});

describe("findProjectConfig", () => {
  it("walks up from a nested directory", () => {
    const directory = temporaryDirectory();
    writeFileSync(join(directory, CONFIG_FILE_NAME), "{}");
    const nested = join(directory, "src", "deep");
    mkdirSync(nested, { recursive: true });
    expect(findProjectConfig(nested)).toBe(join(directory, CONFIG_FILE_NAME));
  });

  it("returns undefined when no config is found", () => {
    const nested = join(temporaryDirectory(), "src");
    mkdirSync(nested, { recursive: true });
    expect(findProjectConfig(nested)).toBeUndefined();
  });
});

describe("resolveConfigPaths", () => {
  it("resolves relative paths against the config directory", () => {
    const directory = temporaryDirectory();
    const loaded = {
      path: join(directory, CONFIG_FILE_NAME),
      directory,
      config: parseProjectConfig(
        JSON.stringify({
          entry: "src/app.ts",
          outDir: "build",
          output: "build/app",
          extNative: ["native/a.json"],
          app: { name: "Demo", icon: "assets/app.png" },
        }),
      ),
    };
    const resolved = resolveConfigPaths(loaded);
    expect(resolved.entry).toBe(join(directory, "src/app.ts"));
    expect(resolved.outDir).toBe(join(directory, "build"));
    expect(resolved.output).toBe(join(directory, "build/app"));
    expect(resolved.extNative).toEqual([join(directory, "native/a.json")]);
    expect(resolved.app?.icon).toBe(join(directory, "assets/app.png"));
    expect(resolved.app?.name).toBe("Demo");
  });

  it("leaves absolute paths untouched and handles a missing app", () => {
    const directory = temporaryDirectory();
    const absolute = join(directory, "elsewhere", "app.ts");
    const loaded = {
      path: join(directory, CONFIG_FILE_NAME),
      directory,
      config: parseProjectConfig(JSON.stringify({ entry: absolute, extensions: ["node"] })),
    };
    const resolved = resolveConfigPaths(loaded);
    expect(resolved.entry).toBe(absolute);
    expect(resolved.app).toBeUndefined();
    expect(resolved.extensions).toEqual(["node"]);
  });
});
