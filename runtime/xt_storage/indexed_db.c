/*
 * xbintsc `storage` extension — IndexedDB core.
 *
 * A focused, synchronous-core subset of the browser IndexedDB API backed by the
 * same SQLite database as `localStorage`: `open`, `deleteDatabase`, databases,
 * transactions, and the object store operations in `indexed_db_store.c`.
 * Requests deliver their result from a microtask, so `onsuccess` / `onerror` /
 * `onupgradeneeded` fire asynchronously exactly like the browser.
 */

#include "indexed_db_internal.h"

/* Forward declaration: `idb_database_transaction` and `idb_open_cb` both build a
 * transaction, whose definition follows the database handle below. */
static xt_value idb_transaction_build(const char *dbname, xt_value names, const char *mode);

/* -- request plumbing ----------------------------------------------------- */

xt_value idb_request_new(void) {
  xt_value req = xt_object_new();
  xt_storage_set(req, "result", XT_UNDEFINED);
  xt_storage_set(req, "error", XT_NULL);
  xt_storage_set_cstr(req, "readyState", "pending");
  xt_storage_set(req, "source", XT_NULL);
  xt_storage_set(req, "transaction", XT_NULL);
  return req;
}

/* Settle a request from a microtask: set `result`/`error`, mark it done, and
 * invoke `onsuccess`/`onerror` (read at fire time, so assigning the handler
 * after the call still works). */
static xt_value idb_complete_cb(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value req = xt_closure_env(env, 0);
  xt_value value = xt_closure_env(env, 1);
  int success = xt_truthy(xt_closure_env(env, 2));
  xt_storage_set_cstr(req, "readyState", "done");
  if (success) {
    xt_storage_set(req, "result", value);
  } else {
    xt_storage_set(req, "error", value);
  }
  const char *prop = success ? "onsuccess" : "onerror";
  const char *type = success ? "success" : "error";
  xt_value cb = xt_object_get_cstr(req, prop);
  if (XT_IS_FUNCTION(cb)) {
    xt_value event = xt_object_new();
    xt_storage_set(event, "target", req);
    xt_storage_set_cstr(event, "type", type);
    xt_value args[1] = {event};
    xt_call_with_this(cb, req, 1, args);
  }
  return XT_UNDEFINED;
}

void idb_complete(xt_value req, int success, xt_value value) {
  xt_value captured[3] = {req, value, xt_bool(success)};
  xt_value task = xt_closure_new((void *)idb_complete_cb, 3, captured);
  xt_value args[1] = {task};
  xt_queue_microtask(1, args);
}

/* Fire a single event property (`oncomplete`, ...) from a microtask. */
static xt_value idb_event_cb(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value target = xt_closure_env(env, 0);
  const char *prop = xt_storage_cstr(xt_closure_env(env, 1));
  const char *type = xt_storage_cstr(xt_closure_env(env, 2));
  xt_value cb = xt_object_get_cstr(target, prop);
  if (XT_IS_FUNCTION(cb)) {
    xt_value event = xt_object_new();
    xt_storage_set(event, "target", target);
    xt_storage_set_cstr(event, "type", type);
    xt_value args[1] = {event};
    xt_call_with_this(cb, target, 1, args);
  }
  return XT_UNDEFINED;
}

static void idb_schedule_event(xt_value target, const char *prop, const char *type) {
  xt_value captured[3] = {target, xt_string_from_cstr(prop), xt_string_from_cstr(type)};
  xt_value task = xt_closure_new((void *)idb_event_cb, 3, captured);
  xt_value args[1] = {task};
  xt_queue_microtask(1, args);
}

/* -- JSON helpers --------------------------------------------------------- */

xt_value idb_encode(xt_value value) { return xt_json_stringify(1, &value); }

xt_value idb_decode(xt_value text) {
  xt_value args[1] = {text};
  return xt_json_parse(1, args);
}

/* -- database metadata ---------------------------------------------------- */

static int64_t idb_version(sqlite3 *db, const char *name) {
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT version FROM __idb_meta WHERE db=?1", -1, &stmt, NULL);
  if (stmt == NULL) return 0;
  xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
  int64_t version = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int64(stmt, 0);
  sqlite3_finalize(stmt);
  return version;
}

static void idb_set_version(sqlite3 *db, const char *name, int64_t version) {
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db,
                     "INSERT INTO __idb_meta(db,version) VALUES(?1,?2)"
                     " ON CONFLICT(db) DO UPDATE SET version=excluded.version",
                     -1, &stmt, NULL);
  if (stmt == NULL) return;
  xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
  sqlite3_bind_int64(stmt, 2, version);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

static int idb_store_exists(sqlite3 *db, const char *name, const char *store) {
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT 1 FROM __idb_stores WHERE db=?1 AND store=?2", -1, &stmt, NULL);
  if (stmt == NULL) return 0;
  xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
  xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
  int exists = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return exists;
}

void idb_store_meta(sqlite3 *db, const char *name, const char *store, xt_value *key_path,
                    int *auto_increment) {
  *key_path = XT_NULL;
  *auto_increment = 0;
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT key_path, auto_increment FROM __idb_stores WHERE db=?1 AND store=?2",
                     -1, &stmt, NULL);
  if (stmt == NULL) return;
  xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
  xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) *key_path = xt_storage_column(stmt, 0);
    *auto_increment = sqlite3_column_int(stmt, 1);
  }
  sqlite3_finalize(stmt);
}

/* -- database handle ------------------------------------------------------ */

static xt_value idb_object_store_names_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__name"));
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "SELECT store FROM __idb_stores WHERE db=?1 ORDER BY rowid", -1, &stmt, NULL);
  xt_value names = xt_array_new(0, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    while (sqlite3_step(stmt) == SQLITE_ROW) xt_array_push(names, xt_storage_column(stmt, 0));
    sqlite3_finalize(stmt);
  }
  return names;
}

static xt_value idb_database_create_object_store(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__name"));
  const char *store = xt_storage_cstr(xt_arg(argc, argv, 0));
  const char *key_path = NULL;
  int auto_increment = 0;
  if (argc >= 2 && XT_IS_OBJECT(argv[1])) {
    xt_value key_path_value = xt_object_get_cstr(argv[1], "keyPath");
    if (XT_IS_STRING(key_path_value)) key_path = xt_storage_cstr(key_path_value);
    auto_increment = xt_truthy(xt_object_get_cstr(argv[1], "autoIncrement"));
  }
  if (idb_store_exists(db, dbname, store)) {
    xt_throw(xt_storage_error("ConstraintError: an object store with that name already exists"));
    return XT_UNDEFINED;
  }
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "INSERT INTO __idb_stores(db,store,key_path,auto_increment) VALUES(?1,?2,?3,?4)",
                     -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    if (key_path != NULL) {
      xt_storage_bind(stmt, 3, xt_string_from_cstr(key_path));
    } else {
      sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, auto_increment);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return idb_store_build(dbname, store);
}

static xt_value idb_database_delete_object_store(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__name"));
  const char *store = xt_storage_cstr(xt_arg(argc, argv, 0));
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __idb_stores WHERE db=?1 AND store=?2", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_prepare_v2(db, "DELETE FROM __idb_records WHERE db=?1 AND store=?2", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(dbname));
    xt_storage_bind(stmt, 2, xt_string_from_cstr(store));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  return XT_UNDEFINED;
}

static xt_value idb_database_transaction(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  sqlite3 *db = xt_storage_db();
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__name"));
  xt_value raw = xt_arg(argc, argv, 0);
  xt_value names = XT_IS_ARRAY(raw) ? raw : xt_array_new(1, &raw);
  int32_t count = xt_array_size(names);
  for (int32_t i = 0; i < count; i++) {
    xt_value store = xt_array_get(names, xt_number((double)i));
    if (!idb_store_exists(db, dbname, xt_storage_cstr(store))) {
      xt_throw(xt_storage_error("NotFoundError: one of the object stores does not exist"));
      return XT_UNDEFINED;
    }
  }
  const char *mode = argc >= 2 ? xt_storage_cstr(argv[1]) : "readonly";
  return idb_transaction_build(dbname, names, mode);
}

static xt_value idb_database_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

static xt_value idb_database_build(const char *name, double version) {
  xt_value dbase = xt_object_new();
  xt_storage_set(dbase, "__name", xt_string_from_cstr(name));
  xt_storage_set_cstr(dbase, "name", name);
  xt_storage_set(dbase, "version", xt_number(version));
  xt_object_define_getter(dbase, xt_string_from_cstr("objectStoreNames"),
                          xt_closure_new((void *)idb_object_store_names_get, 0, NULL));
  xt_storage_method(dbase, "createObjectStore", (void *)idb_database_create_object_store);
  xt_storage_method(dbase, "deleteObjectStore", (void *)idb_database_delete_object_store);
  xt_storage_method(dbase, "transaction", (void *)idb_database_transaction);
  xt_storage_method(dbase, "close", (void *)idb_database_close);
  return dbase;
}

/* -- transaction handle --------------------------------------------------- */

static xt_value idb_transaction_object_store(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  const char *dbname = xt_storage_cstr(xt_object_get_cstr(self, "__db"));
  return idb_store_build(dbname, xt_storage_cstr(xt_arg(argc, argv, 0)));
}

static xt_value idb_transaction_build(const char *dbname, xt_value names, const char *mode) {
  xt_value tx = xt_object_new();
  xt_storage_set(tx, "__db", xt_string_from_cstr(dbname));
  xt_storage_set(tx, "__names", names);
  xt_storage_set_cstr(tx, "mode", mode);
  xt_storage_set(tx, "objectStoreNames", names);
  xt_storage_set(tx, "error", XT_NULL);
  xt_storage_method(tx, "objectStore", (void *)idb_transaction_object_store);
  /* Store operations are applied synchronously, so `oncomplete` can be scheduled
   * immediately; it reads the (possibly later-assigned) handler when it runs. */
  idb_schedule_event(tx, "oncomplete", "complete");
  return tx;
}

/* -- open / delete -------------------------------------------------------- */

static xt_value idb_open_cb(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value req = xt_closure_env(env, 0);
  const char *name = xt_storage_cstr(xt_closure_env(env, 1));
  double version = xt_to_number(xt_closure_env(env, 2));
  sqlite3 *db = xt_storage_db();

  int64_t current = idb_version(db, name);
  double final_version = current > 0 ? (double)current : 0;
  int upgrade = version > (double)current && version > 0;

  if (version == 0) {
    if (current == 0) {
      idb_set_version(db, name, 1);
      final_version = 1;
    }
  } else if (upgrade) {
    final_version = version;
    xt_value dbase = idb_database_build(name, version);
    xt_storage_set(req, "result", dbase);
    xt_value names = xt_array_new(0, NULL);
    xt_storage_set(req, "transaction", idb_transaction_build(name, names, "versionchange"));
    xt_value cb = xt_object_get_cstr(req, "onupgradeneeded");
    if (XT_IS_FUNCTION(cb)) {
      xt_value event = xt_object_new();
      xt_storage_set(event, "target", req);
      xt_storage_set_cstr(event, "type", "upgradeneeded");
      xt_value args[1] = {event};
      xt_call_with_this(cb, req, 1, args);
    }
    idb_set_version(db, name, (int64_t)version);
  }

  idb_complete(req, 1, idb_database_build(name, final_version));
  return XT_UNDEFINED;
}

static xt_value idb_open(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  const char *name = xt_storage_cstr(xt_arg(argc, argv, 0));
  double version = argc >= 2 ? xt_to_number(argv[1]) : 0;
  xt_value req = idb_request_new();
  xt_value captured[3] = {req, xt_string_from_cstr(name), xt_number(version)};
  xt_value task = xt_closure_new((void *)idb_open_cb, 3, captured);
  xt_value args[1] = {task};
  xt_queue_microtask(1, args);
  return req;
}

static xt_value idb_delete_cb(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value req = xt_closure_env(env, 0);
  const char *name = xt_storage_cstr(xt_closure_env(env, 1));
  sqlite3 *db = xt_storage_db();
  sqlite3_stmt *stmt = NULL;
  sqlite3_prepare_v2(db, "DELETE FROM __idb_meta WHERE db=?1", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_prepare_v2(db, "DELETE FROM __idb_stores WHERE db=?1", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_prepare_v2(db, "DELETE FROM __idb_records WHERE db=?1", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  sqlite3_prepare_v2(db, "DELETE FROM __idb_seq WHERE db=?1", -1, &stmt, NULL);
  if (stmt != NULL) {
    xt_storage_bind(stmt, 1, xt_string_from_cstr(name));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }
  idb_complete(req, 1, XT_UNDEFINED);
  return XT_UNDEFINED;
}

static xt_value idb_delete_database(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  const char *name = xt_storage_cstr(xt_arg(argc, argv, 0));
  xt_value req = idb_request_new();
  xt_value captured[2] = {req, xt_string_from_cstr(name)};
  xt_value task = xt_closure_new((void *)idb_delete_cb, 2, captured);
  xt_value args[1] = {task};
  xt_queue_microtask(1, args);
  return req;
}

/* -- the global ----------------------------------------------------------- */

static xt_value g_indexed_db = XT_UNDEFINED;

xt_value xt_storage_indexed_db(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_OBJECT(g_indexed_db)) {
    g_indexed_db = xt_object_new();
    xt_storage_method(g_indexed_db, "open", (void *)idb_open);
    xt_storage_method(g_indexed_db, "deleteDatabase", (void *)idb_delete_database);
    xt_gc_add_root(&g_indexed_db);
  }
  return g_indexed_db;
}
