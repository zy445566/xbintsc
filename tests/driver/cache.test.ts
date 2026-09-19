import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { BuildCache, hashParts, hashString } from "../../src/driver/cache.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xtsc-cache-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("hashing", () => {
  it("is deterministic for the same content", () => {
    expect(hashString("hello")).toBe(hashString("hello"));
    expect(hashString("hello")).not.toBe(hashString("hellO"));
  });

  it("is injective across part boundaries", () => {
    // "ab"+"c" must not collide with "a"+"bc".
    expect(hashParts(["ab", "c"])).not.toBe(hashParts(["a", "bc"]));
  });
});

describe("BuildCache", () => {
  it("reports missing entries as not fresh", () => {
    const cache = new BuildCache(temporaryDirectory());
    expect(cache.isFresh("key", ["/does/not/exist"])).toBe(false);
  });

  it("treats an entry as fresh only when its outputs exist", () => {
    const directory = temporaryDirectory();
    const output = join(directory, "out.bin");
    const cache = new BuildCache(directory);
    cache.record("key", [output]);
    expect(cache.isFresh("key", [output])).toBe(false);
    writeFileSync(output, "x");
    expect(cache.isFresh("key", [output])).toBe(true);
  });

  it("persists entries across instances", () => {
    const directory = temporaryDirectory();
    const output = join(directory, "artifact");
    const first = new BuildCache(directory);
    first.record("key", [output]);
    first.save();
    expect(first.isFresh("key", [output])).toBe(false); // output missing
    writeFileSync(output, "x");
    const second = new BuildCache(directory);
    expect(second.isFresh("key", [output])).toBe(true);
  });

  it("clears entries", () => {
    const directory = temporaryDirectory();
    const cache = new BuildCache(directory);
    cache.record("key", ["x"]);
    expect(cache.entries()).toHaveLength(1);
    cache.clear();
    expect(cache.entries()).toHaveLength(0);
  });

  it("survives a corrupt manifest", () => {
    const directory = temporaryDirectory();
    writeFileSync(join(directory, "build-cache.json"), "{ not json");
    const cache = new BuildCache(directory);
    expect(cache.entries()).toHaveLength(0);
  });
});
