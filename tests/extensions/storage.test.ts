import { describe, expect, it } from "vitest";
import { DiagnosticBag } from "../../src/diagnostics/diagnostic.js";
import { resolveLinkFeatures } from "../../src/driver/link-features.js";
import { collectUsage } from "../../src/driver/usage.js";
import { createDefaultRegistry, coreExtension } from "../../src/extensions/registry.js";
import { hasStorageUsage } from "../../src/extensions/storage.js";
import { parse } from "../helpers.js";

describe("core extension storage globals", () => {
  it("exposes the three storage globals", () => {
    const globals = coreExtension.globalValues?.() ?? {};
    expect(globals.localStorage?.symbol).toBe("xt_storage_local");
    expect(globals.sessionStorage?.symbol).toBe("xt_storage_session");
    expect(globals.indexedDB?.symbol).toBe("xt_storage_indexed_db");
  });

  it("declares the storage link feature carrying the SQLite sources", () => {
    const features = coreExtension.linkFeatures?.() ?? [];
    const storage = features.find((feature) => feature.name === "storage");
    expect(storage).toBeDefined();
    const sources = storage?.runtimeSources?.() ?? [];
    expect(sources.some((source) => /sqlite3\.c$/.test(source))).toBe(true);
    expect(sources.some((source) => /xt_storage[\\/]storage\.c$/.test(source))).toBe(true);
    expect(sources.some((source) => /xt_storage[\\/]indexed_db\.c$/.test(source))).toBe(true);
    expect(sources.some((source) => /xt_storage[\\/]indexed_db_store\.c$/.test(source))).toBe(true);
  });
});

describe("hasStorageUsage", () => {
  const usage = (text: string): ReturnType<typeof collectUsage> =>
    collectUsage(parse(text).file);

  it("detects localStorage, sessionStorage and indexedDB", () => {
    expect(hasStorageUsage(usage("localStorage.setItem('k', 'v');"))).toBe(true);
    expect(hasStorageUsage(usage("const n = sessionStorage.length;"))).toBe(true);
    expect(hasStorageUsage(usage("indexedDB.open('db', 1);"))).toBe(true);
  });

  it("detects the globals reached through window or self", () => {
    expect(hasStorageUsage(usage("window.localStorage.clear();"))).toBe(true);
    expect(hasStorageUsage(usage("self.indexedDB.deleteDatabase('db');"))).toBe(true);
  });

  it("does not fire for a program that never touches storage", () => {
    expect(hasStorageUsage(usage("console.log('hello');"))).toBe(false);
    expect(hasStorageUsage(usage("const db = { open() {} }; db.open();"))).toBe(false);
  });

  it("is selected by resolveLinkFeatures only for a storage-using program", () => {
    const registry = createDefaultRegistry();
    const bag = new DiagnosticBag();

    const using = resolveLinkFeatures(registry, usage("localStorage.clear();"), {}, bag);
    expect(using.map((feature) => feature.name)).toContain("storage");

    const unused = resolveLinkFeatures(registry, usage("console.log(1);"), {}, bag);
    expect(unused.map((feature) => feature.name)).not.toContain("storage");
    expect(bag.diagnostics).toEqual([]);
  });
});
