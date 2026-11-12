/*
 * xbintsc `storage` extension — Web Storage (localStorage / sessionStorage).
 *
 * Both objects share the Storage surface (`getItem`, `setItem`, `removeItem`,
 * `clear`, `key`, `length`) but address different databases: localStorage uses
 * the configured backend (in-memory by default, or the file embedded by the
 * driver), while sessionStorage always uses a private in-memory database.
 *
 * The backend selection is compile-time: the driver embeds `xt_storage_backend`
 * and `xt_storage_path` (see src/driver/storage.ts), and this unit reads them
 * the first time a database is opened.
 */

#include "storage_internal.h"

/* Embedded configuration symbols. Strong definitions come from the object the
 * driver links for every program that enables the storage extension. */
extern const unsigned int xt_storage_backend;
extern const unsigned char xt_storage_path[];
extern const unsigned long long xt_storage_path_len;

static sqlite3 *g_db = NULL;
static sqlite3 *g_session_db = NULL;

static void storage_init_schema(sqlite3 *db) {
  const char *sql =
      "CREATE TABLE IF NOT EXISTS __web_storage("
      " scope TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
      " PRIMARY KEY(scope, key));"
      "CREATE TABLE IF NOT EXISTS __idb_meta("
      " db TEXT PRIMARY KEY, version INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS __idb_stores("
      " db TEXT NOT NULL, store TEXT NOT NULL, key_path TEXT,"
      " auto_increment INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(db, store));"
      "CREATE TABLE IF NOT EXISTS __idb_records("
      " db TEXT NOT NULL, store TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL,"
      " PRIMARY KEY(db, store, key));"
      "CREATE TABLE IF NOT EXISTS __idb_seq("
      " db TEXT NOT NULL, store TEXT NOT NULL, seq INTEGER NOT NULL,"
      " PRIMARY KEY(db, store));";
  sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static void storage_open(sqlite3 **out, const char *path) {
  if (*out != NULL) return;
  if (sqlite3_open(path, out) != SQLITE_OK) {
    const char *message = sqlite3_errmsg(*out);
    sqlite3_close(*out);
    *out = NULL;
    xt_throw(xt_storage_error(message != NULL ? message : "storage: cannot open database"));
    return;
  }
  storage_init_schema(*out);
}

sqlite3 *xt_storage_db(void) {
  const char *path = xt_storage_backend != 0 ? (const char *)xt_storage_path : ":memory:";
  storage_open(&g_db, path);
  return g_db;
}

sqlite3 *xt_storage_session_db(void) {
  storage_open(&g_session_db, ":memory:");
  return g_session_db;
}

/* -- Storage surface ------------------------------------------------------ */

static sqlite3 *storage_db_for(xt_value self) {
  const char *scope = xt_storage_cstr(xt_object_get_cstr(self, "__scope"));
  return scope != NULL && strcmp(scope, "session") == 0 ? xt_storage_session_db() : xt_storage_db();
}

static xt_value storage_scope_of(xt_value self) {
  return xt_object_get_cstr(self, "__scope");
}

static xt_value storage_get_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = storage_db_for(self);
  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, "SELECT value FROM __web_storage WHERE scope=?1 AND key=?2", -1, &stmt, NULL) !=
      SQLITE_OK) {
    return XT_NULL;
  }
  xt_storage_bind(stmt, 1, storage_scope_of(self));
  xt_storage_bind(stmt, 2, xt_to_string(xt_arg(argc, argv, 0)));
  xt_value result = XT_NULL;
  if (sqlite3_step(stmt) == SQLITE_ROW) result = xt_storage_column(stmt, 0);
  sqlite3_finalize(stmt);
  return result;
}

static xt_value storage_set_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = storage_db_for(self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db,
                     "INSERT INTO __web_storage(scope,key,value) VALUES(?1,?2,?3)"
                     " ON CONFLICT(scope,key) DO UPDATE SET value=excluded.value",
                     -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, storage_scope_of(self));
    xt_storage_bind(stmt, 2, xt_to_string(xt_arg(argc, argv, 0)));
    xt_storage_bind(stmt, 3, xt_to_string(xt_arg(argc, argv, 1)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return XT_UNDEFINED;
}

static xt_value storage_remove_item(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = storage_db_for(self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __web_storage WHERE scope=?1 AND key=?2", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, storage_scope_of(self));
    xt_storage_bind(stmt, 2, xt_to_string(xt_arg(argc, argv, 0)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return XT_UNDEFINED;
}

static xt_value storage_clear(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = storage_db_for(self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __web_storage WHERE scope=?1", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, storage_scope_of(self));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return XT_UNDEFINED;
}

static xt_value storage_length_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = storage_db_for(self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM __web_storage WHERE scope=?1", -1, &stmt, NULL);
  double count = 0;
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, storage_scope_of(self));
    if (sqlite3_step(stmt) == SQLITE_ROW) count = (double)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }
  return xt_number(count);
}

static xt_value storage_key(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = storage_db_for(self);
  double at = argc > 0 ? xt_to_number(argv[0]) : -1;
  if (at < 0) return XT_NULL;
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT key FROM __web_storage WHERE scope=?1 ORDER BY rowid LIMIT 1 OFFSET ?2",
                     -1, &stmt, NULL);
  xt_value result = XT_NULL;
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, storage_scope_of(self));
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)at);
    if (sqlite3_step(stmt) == SQLITE_ROW) result = xt_storage_column(stmt, 0);
    sqlite3_finalize(stmt);
  }
  return result;
}

static xt_value storage_build(const char *scope) {
  xt_value storage = xt_object_new();
  xt_storage_set(storage, "__scope", xt_string_from_cstr(scope));
  xt_storage_method(storage, "getItem", (void *)storage_get_item);
  xt_storage_method(storage, "setItem", (void *)storage_set_item);
  xt_storage_method(storage, "removeItem", (void *)storage_remove_item);
  xt_storage_method(storage, "clear", (void *)storage_clear);
  xt_storage_method(storage, "key", (void *)storage_key);
  xt_object_define_getter(storage, xt_string_from_cstr("length"),
                          xt_closure_new((void *)storage_length_get, 0, NULL));
  return storage;
}

static xt_value g_local_storage = XT_UNDEFINED;
static xt_value g_session_storage = XT_UNDEFINED;

xt_value xt_storage_local(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_OBJECT(g_local_storage)) {
    g_local_storage = storage_build("local");
    xt_gc_add_root(&g_local_storage);
  }
  return g_local_storage;
}

xt_value xt_storage_session(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_OBJECT(g_session_storage)) {
    g_session_storage = storage_build("session");
    xt_gc_add_root(&g_session_storage);
  }
  return g_session_storage;
}
