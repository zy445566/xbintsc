/*
 * Shared helpers for the xbintsc `storage` extension.
 *
 * localStorage / sessionStorage / indexedDB are implemented on top of a vendored
 * SQLite (runtime/xt_storage/sqlite3). This header owns the small value helpers
 * and the two database handles; the Web Storage surface lives in `storage.c`
 * and the IndexedDB surface in `indexed_db.c`.
 */
#ifndef XT_STORAGE_INTERNAL_H
#define XT_STORAGE_INTERNAL_H

#include "rt_internal.h"
#include "sqlite3/sqlite3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The persistent database (localStorage + indexedDB): the configured backend —
 * an in-memory database by default, or the file embedded by the driver. */
sqlite3 *xt_storage_db(void);
/* The session database (sessionStorage): always in-memory, regardless of the
 * configured backend, because a session never outlives the process. */
sqlite3 *xt_storage_session_db(void);

/* -- small value helpers -------------------------------------------------- */

static inline const char *xt_storage_cstr(xt_value value) {
  const char *data = xt_string_data(xt_to_string(value));
  return data != NULL ? data : "";
}

static inline void xt_storage_set(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_string_from_cstr(key), value);
}

static inline void xt_storage_set_cstr(xt_value object, const char *key, const char *value) {
  xt_storage_set(object, key, xt_string_from_cstr(value));
}

static inline xt_value xt_storage_get(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

static inline void xt_storage_method(xt_value object, const char *name, void *fn) {
  xt_storage_set(object, name, xt_closure_new(fn, 0, NULL));
}

static inline xt_value xt_storage_error(const char *message) {
  return xt_error_from_message(message);
}

/* -- SQLite binding helpers ----------------------------------------------- */

/* Bind a JavaScript string (which may contain embedded NUL bytes) as a length-
 * aware text value. SQLITE_TRANSIENT makes SQLite copy the bytes. */
static inline void xt_storage_bind(sqlite3_stmt *stmt, int index, xt_value value) {
  sqlite3_bind_text(stmt, index, xt_string_data(value), (int)xt_string_length_value(value), SQLITE_TRANSIENT);
}

/* Read a text column as a JavaScript string (length-aware). */
static inline xt_value xt_storage_column(sqlite3_stmt *stmt, int index) {
  return xt_string_new((const char *)sqlite3_column_text(stmt, index), (size_t)sqlite3_column_bytes(stmt, index));
}

#endif /* XT_STORAGE_INTERNAL_H */
