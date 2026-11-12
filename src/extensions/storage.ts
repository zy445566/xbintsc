/**
 * SQLite-backed Web Storage and IndexedDB, contributed as a *link feature*.
 *
 * `localStorage`, `sessionStorage` and `indexedDB` are browser globals made
 * available by the always-on `core` extension (`src/extensions/registry.ts`).
 * The SQLite implementation behind them is heavy, so it is a **link feature**:
 * the driver scans the bundled AST and links it only when the program may
 * actually reach one of the three globals — a program that never touches
 * storage pays nothing for SQLite.
 *
 * The backend is selected at build time from `storage` in
 * `xbintsc.config.json` (default: in-memory; `sessionStorage` is always
 * in-memory). `--feature storage` forces the backend on (for storage reached
 * through names the scan cannot see) and `--no-feature storage` keeps it out.
 */

import { join } from "node:path";
import type { GlobalValue } from "./registry.js";
import type { LinkFeature, ProgramUsage } from "./link-feature.js";
import { findRuntimeDir } from "../driver/paths.js";

/** The browser storage globals resolved by the core extension. */
export const storageGlobals: Readonly<Record<string, GlobalValue>> = {
  localStorage: { symbol: "xt_storage_local" },
  sessionStorage: { symbol: "xt_storage_session" },
  indexedDB: { symbol: "xt_storage_indexed_db" },
};

/** True when the bundled program may reach a Web Storage or IndexedDB global. */
export function hasStorageUsage(usage: ProgramUsage): boolean {
  return (
    usage.identifiers.has("localStorage") ||
    usage.identifiers.has("sessionStorage") ||
    usage.identifiers.has("indexedDB")
  );
}

export const storageLinkFeatures: readonly LinkFeature[] = [
  {
    name: "storage",
    description: "SQLite-backed localStorage, sessionStorage and indexedDB",
    /**
     * The full implementation lives here, not in the extension's runtime
     * sources, so it is compiled and linked only when the feature is active.
     * `storage.c` defines the `xt_storage_local` / `xt_storage_session` globals
     * the code generator reaches for, and `indexed_db.c` / `indexed_db_store.c`
     * define `xt_storage_indexed_db`; all three reference the vendored SQLite.
     */
    runtimeSources: () => [
      join(findRuntimeDir(), "xt_storage", "sqlite3", "sqlite3.c"),
      join(findRuntimeDir(), "xt_storage", "storage.c"),
      join(findRuntimeDir(), "xt_storage", "indexed_db.c"),
      join(findRuntimeDir(), "xt_storage", "indexed_db_store.c"),
    ],
    used: hasStorageUsage,
  },
];
