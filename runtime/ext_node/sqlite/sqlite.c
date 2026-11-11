/*
 * Node.js `sqlite` module for xbintsc.
 *
 * SQLite is not linked, so `DatabaseSync` / `StatementSync` throw when
 * constructed. undici's `SqliteCacheStore` only touches `node:sqlite` lazily
 * and documents a constructor throw when the API is unavailable, so this keeps
 * the module importable without pulling in a SQLite library.
 */

#include "../node_common.h"

xt_value xt_sqlite_database_sync_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:sqlite is not supported in compiled binaries"));
  return XT_UNDEFINED;
}

xt_value xt_sqlite_statement_sync_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("Error: node:sqlite is not supported in compiled binaries"));
  return XT_UNDEFINED;
}
