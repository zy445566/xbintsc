/*
 * xbintsc `storage` extension — IndexedDB object stores.
 *
 * `put` / `add` / `get` / `getAll` / `getKey` / `delete` / `clear` / `count`
 * plus the object store handle itself. Every operation is applied synchronously
 * to SQLite and settles its IDBRequest from a microtask (see indexed_db.c), so
 * the store surface keeps the same call shape as the browser while staying
 * single-threaded.
 */

#include "indexed_db_internal.h"

/* -- key handling --------------------------------------------------------- */

/* Allocate (or increment) the auto-increment sequence for one store. */
static xt_value idb_next_seq(sqlite3 *db, const char *dbname, const char *store) {
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db,
                     "INSERT INTO __idb_seq(db,store,seq) VALUES(?1,?2,1)"
                     " ON CONFLICT(db,store) DO UPDATE SET seq=seq+1 RETURNING seq",
                     -1, &stmt, NULL);
  if (stmt == NULL) return XT_UNDEFINED;
  xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
  xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
  xt_value key = XT_UNDEFINED;
  if (sqlite3_step(stmt) == SQLITE_ROW) key = xt_number((double)sqlite3_column_int64(stmt, 0));
  sqlite3_finalize(stmt);
  return key;
}

/*
 * Resolve the key for a `put`/`add` value: the keyPath property when set, the
 * explicit key otherwise, or a fresh auto-increment key. Returns XT_UNDEFINED
 * when no key could be produced.
 */
static xt_value idb_resolve_key(sqlite3 *db, const char *dbname, const char *store, xt_value value,
                                xt_value explicit_key, xt_value key_path, int auto_increment) {
  xt_value key = XT_UNDEFINED;
  if (!XT_IS_NULL(key_path) && XT_IS_OBJECT(value)) {
    key = xt_get(value, key_path);
  } else if (!XT_IS_UNDEFINED(explicit_key)) {
    key = explicit_key;
  }
  if (XT_IS_UNDEFINED(key) && auto_increment) key = idb_next_seq(db, dbname, store);
  return key;
}

/* Serialize a value for the `value` column (JSON, or "null" when unencodable). */
static xt_value idb_serialize(xt_value value) {
  xt_value serialized = idb_encode(value);
  return XT_IS_UNDEFINED(serialized) ? xt_string_from_cstr("null") : serialized;
}

/* -- operations ----------------------------------------------------------- */

static xt_value idb_store_put(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value value = xt_arg(argc, argv, 0);
  xt_value explicit_key = argc >= 2 ? xt_arg(argc, argv, 1) : XT_UNDEFINED;
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);

  xt_value key_path = XT_NULL;
  int auto_increment = 0;
  idb_store_meta(db, dbname, store, &key_path, &auto_increment);
  xt_value key = idb_resolve_key(db, dbname, store, value, explicit_key, key_path, auto_increment);
  if (XT_IS_UNDEFINED(key)) {
    idb_complete(req, 0, xt_storage_error("DataError: no key was supplied"));
    return req;
  }

  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db,
                     "INSERT INTO __idb_records(db,store,key,value) VALUES(?1,?2,?3,?4)"
                     " ON CONFLICT(db,store,key) DO UPDATE SET value=excluded.value",
                     -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    xt_storage_bind(stmt, 3, idb_encode(key));
    xt_storage_bind(stmt, 4, idb_serialize(value));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, key);
  return req;
}

static xt_value idb_store_add(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value value = xt_arg(argc, argv, 0);
  xt_value explicit_key = argc >= 2 ? xt_arg(argc, argv, 1) : XT_UNDEFINED;
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);

  xt_value key_path = XT_NULL;
  int auto_increment = 0;
  idb_store_meta(db, dbname, store, &key_path, &auto_increment);
  xt_value key = idb_resolve_key(db, dbname, store, value, explicit_key, key_path, auto_increment);
  if (XT_IS_UNDEFINED(key)) {
    idb_complete(req, 0, xt_storage_error("DataError: no key was supplied"));
    return req;
  }

  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "INSERT INTO __idb_records(db,store,key,value) VALUES(?1,?2,?3,?4)", -1, &stmt,
                     NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    xt_storage_bind(stmt, 3, idb_encode(key));
    xt_storage_bind(stmt, 4, idb_serialize(value));
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
      idb_complete(req, 0, xt_storage_error("ConstraintError: key already exists"));
      return req;
    }
  }
  idb_complete(req, 1, key);
  return req;
}

static xt_value idb_store_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT value FROM __idb_records WHERE db=?1 AND store=?2 AND key=?3", -1,
                     &stmt, NULL);
  xt_value result = XT_UNDEFINED;
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    xt_storage_bind(stmt, 3, idb_encode(xt_arg(argc, argv, 0)));
    if (sqlite3_step(stmt) == SQLITE_ROW) result = idb_decode(xt_storage_column(stmt, 0));
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, result);
  return req;
}

static xt_value idb_store_get_all(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT value FROM __idb_records WHERE db=?1 AND store=?2 ORDER BY key", -1,
                     &stmt, NULL);
  xt_value results = xt_array_new(0, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      xt_array_push(results, idb_decode(xt_storage_column(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, results);
  return req;
}

static xt_value idb_store_get_key(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT key FROM __idb_records WHERE db=?1 AND store=?2 AND key=?3", -1, &stmt,
                     NULL);
  xt_value result = XT_UNDEFINED;
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    xt_storage_bind(stmt, 3, idb_encode(xt_arg(argc, argv, 0)));
    if (sqlite3_step(stmt) == SQLITE_ROW) result = idb_decode(xt_storage_column(stmt, 0));
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, result);
  return req;
}

static xt_value idb_store_delete(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __idb_records WHERE db=?1 AND store=?2 AND key=?3", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    xt_storage_bind(stmt, 3, idb_encode(xt_arg(argc, argv, 0)));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, XT_UNDEFINED);
  return req;
}

static xt_value idb_store_clear(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __idb_records WHERE db=?1 AND store=?2", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, XT_UNDEFINED);
  return req;
}

static xt_value idb_store_count(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  const char *store = xt_storage_cstr(xt_object_get_cstr(self, "__store"));
  xt_value req = idb_request_new();
  xt_storage_set(req, "source", self);
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM __idb_records WHERE db=?1 AND store=?2", -1, &stmt, NULL);
  double count = 0;
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    if (sqlite3_step(stmt) == SQLITE_ROW) count = (double)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, xt_number(count));
  return req;
}

/* -- handle --------------------------------------------------------------- */

static xt_value idb_store_key_path_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value key_path = XT_NULL;
  int auto_increment = 0;
  idb_store_meta(xt_storage_db(), xt_storage_cstr(xt_object_get_cstr(self, "__db")),
                 xt_storage_cstr(xt_object_get_cstr(self, "__store")), &key_path, &auto_increment);
  return key_path;
}

static xt_value idb_store_auto_increment_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value key_path = XT_NULL;
  int auto_increment = 0;
  idb_store_meta(xt_storage_db(), xt_storage_cstr(xt_object_get_cstr(self, "__db")),
                 xt_storage_cstr(xt_object_get_cstr(self, "__store")), &key_path, &auto_increment);
  return xt_bool(auto_increment);
}

xt_value idb_store_build(const char *dbname, const char *store) {
  xt_value handle = xt_object_new();
  xt_storage_set(handle, "__db", xt_string_from_cstr(dbname));
  xt_storage_set(handle, "__store", xt_string_from_cstr(store));
  xt_storage_set_cstr(handle, "name", store);
  xt_storage_set(handle, "indexNames", xt_array_new(0, NULL));
  xt_object_define_getter(handle, xt_string_from_cstr("keyPath"),
                          xt_closure_new((void *)idb_store_key_path_get, 0, NULL));
  xt_object_define_getter(handle, xt_string_from_cstr("autoIncrement"),
                          xt_closure_new((void *)idb_store_auto_increment_get, 0, NULL));
  xt_storage_method(handle, "put", (void *)idb_store_put);
  xt_storage_method(handle, "add", (void *)idb_store_add);
  xt_storage_method(handle, "get", (void *)idb_store_get);
  xt_storage_method(handle, "getAll", (void *)idb_store_get_all);
  xt_storage_method(handle, "getKey", (void *)idb_store_get_key);
  xt_storage_method(handle, "delete", (void *)idb_store_delete);
  xt_storage_method(handle, "clear", (void *)idb_store_clear);
  xt_storage_method(handle, "count", (void *)idb_store_count);
  return handle;
}
