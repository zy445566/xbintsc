/*
 * Internal declarations shared between the IndexedDB translation units.
 *
 * `indexed_db.c` owns the request plumbing, JSON helpers, database/transaction
 * handles and the open/delete lifecycle; `indexed_db_store.c` owns the object
 * store operations (`put`, `get`, `getAll`, …) and builds the store handle.
 */
#ifndef XT_STORAGE_INDEXED_DB_INTERNAL_H
#define XT_STORAGE_INDEXED_DB_INTERNAL_H

#include "storage_internal.h"

/* A fresh IDBRequest with its result/error/readyState initialised. */
xt_value idb_request_new(void);
/* Settle `req` from a microtask and invoke its `onsuccess`/`onerror`. */
void idb_complete(xt_value req, int success, xt_value value);
/* JSON stringify / parse used to serialise keys and values. */
xt_value idb_encode(xt_value value);
xt_value idb_decode(xt_value text);
/* Read the keyPath / autoIncrement of a store (NULL keyPath + 0 when absent). */
void idb_store_meta(sqlite3 *db, const char *name, const char *store, xt_value *key_path,
                    int *auto_increment);
/* Build an IDBObjectStore handle (defined in indexed_db_store.c). */
xt_value idb_store_build(const char *dbname, const char *store);

#endif /* XT_STORAGE_INDEXED_DB_INTERNAL_H */
