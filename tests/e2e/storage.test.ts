/**
 * End-to-end coverage for the `storage` extension: SQLite-backed
 * localStorage / sessionStorage / indexedDB, the in-memory default and the
 * file backend (including persistence across two processes). Skipped when no
 * clang >= 16 is available, like every suite that compiles a real binary.
 */

import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterEach, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-storage-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

interface RunStorageOptions {
  readonly directory: string;
  readonly storage?: { readonly backend: "memory" | "file"; readonly path?: string };
}

function runStorage(source: string, options: RunStorageOptions): string {
  const entry = join(options.directory, `app_${Math.random().toString(36).slice(2)}.ts`);
  writeFileSync(entry, source);
  const registry = createDefaultRegistry();
  const result = build(entry, {
    emit: "exe",
    outDir: join(options.directory, "out"),
    cacheDir: join(options.directory, ".cache"),
    extensions: registry,
    ...(options.storage ? { storage: options.storage } : {}),
  });
  expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
  const executed = spawnSync(result.outputPath, [], { encoding: "utf8" });
  expect(executed.status).toBe(0);
  return executed.stdout.trim();
}

describeWithClang("storage extension", () => {
  it("round-trips localStorage and keeps sessionStorage in memory", () => {
    const output = runStorage(
      `
        localStorage.setItem("k", "v");
        localStorage.setItem("k", "v2");
        console.log(localStorage.getItem("k"), localStorage.length, localStorage.key(0));
        localStorage.removeItem("k");
        console.log(localStorage.getItem("k"), localStorage.length);
        sessionStorage.setItem("s", "x");
        console.log(sessionStorage.getItem("s"));
      `,
      { directory: temporaryDirectory() },
    );
    expect(output).toBe("v2 1 k\nnull 0\nx");
  });

  it("stores and retrieves objects through indexedDB", () => {
    const output = runStorage(
      `
        const req = indexedDB.open("db", 1);
        req.onupgradeneeded = () => {
          req.result.createObjectStore("users", { keyPath: "id" });
        };
        req.onsuccess = () => {
          const db = req.result;
          const tx = db.transaction("users", "readwrite");
          const store = tx.objectStore("users");
          store.put({ id: 1, name: "ada" });
          store.add({ id: 2, name: "grace" });
          store.get(1).onsuccess = (e) => {
            console.log(e.target.result.name);
          };
          store.getAll().onsuccess = (e) => {
            console.log(e.target.result.length, e.target.result[0].name);
          };
        };
      `,
      { directory: temporaryDirectory() },
    );
    expect(output).toBe("ada\n2 ada");
  });

  it("persists localStorage across processes with a file backend", () => {
    const directory = temporaryDirectory();
    mkdirSync(join(directory, "data"), { recursive: true });
    const storage = { backend: "file" as const, path: join(directory, "data", "app.db") };

    runStorage(`localStorage.setItem("k", "persisted");`, { directory, storage });
    const output = runStorage(`console.log(localStorage.getItem("k"));`, { directory, storage });
    expect(output).toBe("persisted");
  });
});
