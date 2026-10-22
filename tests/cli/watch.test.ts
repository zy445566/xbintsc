/**
 * `xbintsc watch` fingerprints the entry tree with a polling scan. These tests
 * cover the scan boundaries (which files count) and the rebuild trigger without
 * starting a real CLI process.
 */

import { mkdirSync, mkdtempSync, rmSync, utimesSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";
import { fingerprintTree, startWatching } from "../../src/cli/watch.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-watch-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  vi.useRealTimers();
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("watch fingerprint", () => {
  it("changes when a watched source file changes", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "app.ts");
    writeFileSync(entry, "console.log(1);");
    const before = fingerprintTree(directory);

    writeFileSync(entry, "console.log(2);");
    expect(fingerprintTree(directory)).not.toBe(before);
  });

  it("tracks an mtime-only change", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "app.ts");
    writeFileSync(entry, "console.log(1);");
    const before = fingerprintTree(directory);

    const later = new Date(Date.now() + 5000);
    utimesSync(entry, later, later);
    expect(fingerprintTree(directory)).not.toBe(before);
  });

  it("ignores unrelated extensions, build output and node_modules", () => {
    const directory = temporaryDirectory();
    writeFileSync(join(directory, "app.ts"), "console.log(1);");
    const before = fingerprintTree(directory);

    writeFileSync(join(directory, "notes.md"), "not source");
    mkdirSync(join(directory, "build"));
    writeFileSync(join(directory, "build", "app.js"), "output");
    mkdirSync(join(directory, "node_modules", "pkg"), { recursive: true });
    writeFileSync(join(directory, "node_modules", "pkg", "index.ts"), "dependency");

    expect(fingerprintTree(directory)).toBe(before);
  });
});

describe("startWatching", () => {
  it("rebuilds only after the fingerprint changes", () => {
    vi.useFakeTimers();
    const scans = ["a", "a", "b", "b", "c"];
    let index = 0;
    const rebuild = vi.fn();
    const stop = startWatching({
      scan: () => scans[Math.min(index++, scans.length - 1)]!,
      intervalMs: 100,
      onRebuild: rebuild,
    });

    vi.advanceTimersByTime(300);
    expect(rebuild).toHaveBeenCalledTimes(1);
    vi.advanceTimersByTime(200);
    expect(rebuild).toHaveBeenCalledTimes(2);

    stop();
    vi.advanceTimersByTime(1000);
    expect(rebuild).toHaveBeenCalledTimes(2);
  });
});
