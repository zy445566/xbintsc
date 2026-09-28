import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { classifyDependency } from "../../src/driver/bundler/resolve.js";

const directories: string[] = [];
const NONE: ReadonlySet<string> = new Set();

afterEach(() => {
  for (const directory of directories.splice(0)) rmSync(directory, { recursive: true, force: true });
});

/** Create a throwaway directory tree from `{ relativePath: contents }`. */
function writeFiles(files: Record<string, string>): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-resolve-"));
  directories.push(directory);
  for (const [name, content] of Object.entries(files)) {
    const path = join(directory, name);
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, content);
  }
  return directory;
}

function resolveFile(directory: string, specifier: string): ReturnType<typeof classifyDependency> {
  return classifyDependency(directory, specifier, NONE);
}

describe("classifyDependency", () => {
  it("resolves relative, extensionless and directory-index files", () => {
    const directory = writeFiles({ "util.ts": "", "other.tsx": "", "dir/index.ts": "" });
    expect(resolveFile(directory, "./util")).toEqual({ kind: "file", path: join(directory, "util.ts") });
    expect(resolveFile(directory, "./other")).toEqual({ kind: "file", path: join(directory, "other.tsx") });
    expect(resolveFile(directory, "./dir")).toEqual({ kind: "file", path: join(directory, "dir", "index.ts") });
  });

  it("maps .js-family specifiers onto TypeScript sources", () => {
    const directory = writeFiles({ "foo.ts": "", "comp.tsx": "", "mod.mts": "", "common.cts": "" });
    expect(resolveFile(directory, "./foo.js")).toEqual({ kind: "file", path: join(directory, "foo.ts") });
    expect(resolveFile(directory, "./comp.jsx")).toEqual({ kind: "file", path: join(directory, "comp.tsx") });
    expect(resolveFile(directory, "./mod.mjs")).toEqual({ kind: "file", path: join(directory, "mod.mts") });
    expect(resolveFile(directory, "./common.cjs")).toEqual({ kind: "file", path: join(directory, "common.cts") });
  });

  it("uses a directory package.json `main`", () => {
    const directory = writeFiles({
      "pkg/package.json": JSON.stringify({ main: "lib/entry.js" }),
      "pkg/lib/entry.ts": "",
    });
    expect(resolveFile(directory, "./pkg")).toEqual({ kind: "file", path: join(directory, "pkg", "lib", "entry.ts") });
  });

  it("treats node: and explicit externals as external", () => {
    const directory = writeFiles({});
    expect(classifyDependency(directory, "node:fs", NONE)).toEqual({ kind: "external" });
    expect(classifyDependency(directory, "react", new Set(["react"]))).toEqual({ kind: "external" });
  });

  it("reports a missing relative path and an unresolved bare specifier", () => {
    const directory = writeFiles({});
    expect(resolveFile(directory, "./nope")).toEqual({ kind: "missing" });
    expect(resolveFile(directory, "nonexistent-pkg")).toEqual({ kind: "external" });
  });

  it("resolves a package with a string exports field", () => {
    const directory = writeFiles({
      "node_modules/pkg/package.json": JSON.stringify({ exports: "./src/index.ts" }),
      "node_modules/pkg/src/index.ts": "",
    });
    expect(resolveFile(directory, "pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "src", "index.ts"),
    });
  });

  it("resolves a conditional exports map without subpath keys", () => {
    const directory = writeFiles({
      "node_modules/pkg/package.json": JSON.stringify({
        exports: { import: "./esm/index.ts", require: "./cjs/index.ts" },
      }),
      "node_modules/pkg/esm/index.ts": "",
      "node_modules/pkg/cjs/index.ts": "",
    });
    expect(resolveFile(directory, "pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "esm", "index.ts"),
    });
  });

  it("resolves root and explicit subpath exports", () => {
    const directory = writeFiles({
      "node_modules/pkg/package.json": JSON.stringify({ exports: { ".": "./main.ts", "./sub": "./sub.ts" } }),
      "node_modules/pkg/main.ts": "",
      "node_modules/pkg/sub.ts": "",
    });
    expect(resolveFile(directory, "pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "main.ts"),
    });
    expect(resolveFile(directory, "pkg/sub")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "sub.ts"),
    });
  });

  it("resolves wildcard exports with string, array and object targets", () => {
    const directory = writeFiles({
      "node_modules/string-pkg/package.json": JSON.stringify({ exports: { "./*": "./dist/*.js" } }),
      "node_modules/string-pkg/dist/a.ts": "",
      "node_modules/array-pkg/package.json": JSON.stringify({ exports: { "./*": ["./dist/*.mjs", "./dist/*.js"] } }),
      "node_modules/array-pkg/dist/b.mts": "",
      "node_modules/object-pkg/package.json": JSON.stringify({
        exports: { "./*": { import: "./dist/*.ts", default: "./dist/*.js" } },
      }),
      "node_modules/object-pkg/dist/c.ts": "",
    });
    expect(resolveFile(directory, "string-pkg/a")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "string-pkg", "dist", "a.ts"),
    });
    expect(resolveFile(directory, "array-pkg/b")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "array-pkg", "dist", "b.mts"),
    });
    expect(resolveFile(directory, "object-pkg/c")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "object-pkg", "dist", "c.ts"),
    });
  });

  it("falls back to `module`, `main` and `index` entries", () => {
    const directory = writeFiles({
      "node_modules/module-pkg/package.json": JSON.stringify({ module: "./esm.ts" }),
      "node_modules/module-pkg/esm.ts": "",
      "node_modules/main-pkg/package.json": JSON.stringify({ main: "./main.ts" }),
      "node_modules/main-pkg/main.ts": "",
      "node_modules/index-pkg/package.json": JSON.stringify({}),
      "node_modules/index-pkg/index.ts": "",
    });
    expect(resolveFile(directory, "module-pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "module-pkg", "esm.ts"),
    });
    expect(resolveFile(directory, "main-pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "main-pkg", "main.ts"),
    });
    expect(resolveFile(directory, "index-pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "index-pkg", "index.ts"),
    });
  });

  it("falls back when an exports target does not exist", () => {
    const directory = writeFiles({
      "node_modules/pkg/package.json": JSON.stringify({ exports: ["./missing.ts"], main: "./real.ts" }),
      "node_modules/pkg/real.ts": "",
    });
    expect(resolveFile(directory, "pkg")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "real.ts"),
    });
  });

  it("ignores malformed, non-object and empty exports values", () => {
    const directory = writeFiles({
      "node_modules/bad/package.json": "{ not json",
      "node_modules/bad/index.ts": "",
      "node_modules/scalar/package.json": JSON.stringify({ exports: 123 }),
      "node_modules/scalar/index.ts": "",
      "node_modules/empty/package.json": JSON.stringify({ exports: [] }),
      "node_modules/empty/index.ts": "",
      "node_modules/nil/package.json": JSON.stringify({ exports: null }),
      "node_modules/nil/index.ts": "",
    });
    expect(resolveFile(directory, "bad")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "bad", "index.ts"),
    });
    expect(resolveFile(directory, "scalar")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "scalar", "index.ts"),
    });
    expect(resolveFile(directory, "empty")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "empty", "index.ts"),
    });
    expect(resolveFile(directory, "nil")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "nil", "index.ts"),
    });
  });

  it("resolves scoped package subpaths", () => {
    const directory = writeFiles({
      "node_modules/@scope/pkg/package.json": JSON.stringify({ exports: { "./sub": "./sub.ts" } }),
      "node_modules/@scope/pkg/sub.ts": "",
    });
    expect(resolveFile(directory, "@scope/pkg/sub")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "@scope", "pkg", "sub.ts"),
    });
  });

  it("walks up to a parent node_modules", () => {
    const directory = writeFiles({
      "node_modules/top/package.json": JSON.stringify({ main: "./index.ts" }),
      "node_modules/top/index.ts": "",
      "a/b/c/.keep": "",
    });
    expect(classifyDependency(join(directory, "a", "b", "c"), "top", NONE)).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "top", "index.ts"),
    });
  });

  it("resolves packages without a package.json and subpaths without exports", () => {
    const directory = writeFiles({
      "node_modules/bare/index.ts": "",
      "node_modules/plain/sub.ts": "",
      "empty-dir/.keep": "",
    });
    expect(resolveFile(directory, "bare")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "bare", "index.ts"),
    });
    expect(resolveFile(directory, "plain/sub")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "plain", "sub.ts"),
    });
    /* A directory that exists but has no index entry is unresolved. */
    expect(resolveFile(directory, "./empty-dir")).toEqual({ kind: "missing" });
  });

  it("ignores a wildcard key that does not match the subpath", () => {
    const directory = writeFiles({
      "node_modules/pkg/package.json": JSON.stringify({ exports: { "./x/*": "./dist/*.ts" } }),
      "node_modules/pkg/index.ts": "",
      "node_modules/pkg/y.ts": "",
    });
    /* `pkg/y` misses the `./x/*` pattern, so it falls back to the literal path. */
    expect(resolveFile(directory, "pkg/y")).toEqual({
      kind: "file",
      path: join(directory, "node_modules", "pkg", "y.ts"),
    });
  });
});
