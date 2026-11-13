/*
 * xbintsc runtime — internal interface of the wasm3-backed WebAssembly feature.
 *
 * `wasm_api.c` owns the JS-facing namespace and the object prototypes;
 * `wasm_module.c` parses bytes into the metadata a `WebAssembly.Module` needs;
 * `wasm_instance.c` links, instantiates and bridges calls in both directions.
 * The split keeps each translation unit inside the repository's file budget.
 */

#ifndef XT_WASM_H
#define XT_WASM_H

#include "../rt_internal.h"

#include "wasm3/m3_env.h"
#include "wasm3/wasm3.h"

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wasm_module wasm_module;
typedef struct wasm_instance wasm_instance;
typedef struct wasm_memory wasm_memory;
typedef struct wasm_import wasm_import;

/* Object kinds with a shared prototype and constructor. */
enum {
  XT_WASM_KIND_MODULE = 0,
  XT_WASM_KIND_INSTANCE = 1,
  XT_WASM_KIND_MEMORY = 2,
  XT_WASM_KIND_TABLE = 3,
  XT_WASM_KIND_GLOBAL = 4,
  XT_WASM_KIND_COUNT = 5,
};

typedef struct wasm_export_desc {
  char *name;
  uint32_t name_length;
  uint8_t kind;
  uint32_t index;
} wasm_export_desc;

typedef struct wasm_import_desc {
  char *module;
  char *name;
  uint32_t name_length;
  uint8_t kind;
} wasm_import_desc;

struct wasm_module {
  uint8_t *bytes;
  uint32_t length;
  wasm_export_desc *exports;
  uint32_t export_count;
  wasm_import_desc *imports;
  uint32_t import_count;
};

struct wasm_import {
  wasm_instance *instance;
  xt_value function;
  uint32_t function_index;
  struct wasm_import *next;
};

struct wasm_memory {
  wasm_instance *instance;
  uint32_t index;
  xt_value object;
  xt_value buffer;
  uint32_t buffer_bytes;
  struct wasm_memory *next;
};

struct wasm_instance {
  IM3Environment environment;
  IM3Runtime runtime;
  IM3Module module;
  wasm_module *source;
  xt_value object;
  xt_value exports;
  xt_value imports;
  xt_value pending_exception;
  wasm_import *import_functions;
  wasm_memory *memories;
  struct wasm_instance *next;
};

/* -- prototype / hidden-key helpers (wasm_api.c) -------------------------- */
xt_value xt_wasm_object_new(int kind);
xt_value xt_wasm_constructor(int kind);
xt_value xt_wasm_module_key(void);
xt_value xt_wasm_instance_key(void);
xt_value xt_wasm_memory_key(void);
xt_value xt_wasm_ptr_value(const void *pointer);
void *xt_wasm_value_ptr(xt_value value);
/** Register/prototype setup; safe to call repeatedly. */
void xt_wasm_api_init(void);

/* -- value and error helpers (wasm_values.c) ------------------------------ */
void xt_wasm_set_error(char **error, const char *format, ...);
xt_value xt_wasm_error(const char *kind, const char *message);
xt_value xt_wasm_type_error(const char *message);
/** Copy a BufferSource (`ArrayBuffer` or typed-array view) into fresh memory. */
int xt_wasm_bytes_from_value(xt_value value, uint8_t **bytes, uint32_t *length,
                             char **error);
int xt_wasm_is_buffer_source(xt_value value);
int64_t xt_wasm_to_i64(xt_value value);
xt_value xt_wasm_from_i64(int64_t value);
xt_value xt_wasm_number_arg(int32_t argc, xt_value *argv, int32_t index);
xt_value xt_wasm_object_arg(int32_t argc, xt_value *argv, int32_t index);

/* -- module metadata (wasm_module.c) -------------------------------------- */
wasm_module *xt_wasm_module_parse(const uint8_t *bytes, uint32_t length, char **error);
void xt_wasm_module_release(wasm_module *module);
wasm_module *xt_wasm_module_of(xt_value value);
xt_value xt_wasm_module_object(wasm_module *module, char **error);
xt_value xt_wasm_compile_to_module(xt_value bytes, char **error);
xt_value xt_wasm_module_exports_value(const wasm_module *module);
xt_value xt_wasm_module_imports_value(const wasm_module *module);
const wasm_export_desc *xt_wasm_module_find_export(const wasm_module *module,
                                                   const char *name);

/* -- instances and calls (wasm_instance.c) -------------------------------- */
xt_value xt_wasm_instance_value(wasm_module *module, xt_value imports, char **error);
xt_value xt_wasm_instantiate_value(xt_value module_or_bytes, xt_value imports,
                                   char **error);
int xt_wasm_is_instance(xt_value value);
void xt_wasm_sync_memories(wasm_instance *instance, int into_wasm);
int xt_wasm_memory_pointer(xt_value object, wasm_memory **out);
uint32_t xt_wasm_memory_index(wasm_memory *memory);
void xt_wasm_memory_grow(wasm_memory *memory, uint32_t delta_pages, char **error);
xt_value xt_wasm_memory_buffer(wasm_memory *memory);
const char *xt_wasm_memory_name(wasm_memory *memory);

/* -- namespace entry points (wasm_api.c) ---------------------------------- */
xt_value xt_wasm_dispatch_static(xt_value name, int32_t argc, xt_value *argv);
xt_value xt_wasm_build_object(int32_t argc, xt_value *argv);

#ifdef __cplusplus
}
#endif

#endif /* XT_WASM_H */
