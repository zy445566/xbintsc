/**
 * Node's `sqlite` module: `DatabaseSync` / `StatementSync` (both throw, since
 * SQLite is not linked).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  DatabaseSync: { symbol: "xt_sqlite_database_sync_ctor", isConstructor: true },
  StatementSync: { symbol: "xt_sqlite_statement_sync_ctor", isConstructor: true },
};

export const sqliteModule: NodeModule = {
  name: "sqlite",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/sqlite/sqlite.c")],
  builtins: () => ({}),
  exports: () => exports,
};
