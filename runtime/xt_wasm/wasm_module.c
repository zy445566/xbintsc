/*
 * xbintsc runtime — WebAssembly module parsing and metadata.
 *
 * A `WebAssembly.Module` keeps its bytes because wasm3 parses lazily: the
 * interpreter holds a pointer into them and compiles each function body on
 * first use. Parsing here validates eagerly and copies the export/import
 * descriptors the JS `Module` statics expose; the wasm3 module itself is
 * discarded after inspection.
 */

#include "wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *xt_wasm_dup_range(const char *text, uint32_t length) {
  if (text == NULL) return NULL;
  char *copy = (char *)malloc((size_t)length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, text, length);
  copy[length] = '\0';
  return copy;
}

static uint32_t xt_wasm_text_length(const char *text) {
  return text == NULL ? 0 : (uint32_t)strlen(text);
}

static const char *xt_wasm_export_kind_name(uint8_t kind) {
  switch (kind) {
    case d_externalKind_function:
      return "function";
    case d_externalKind_table:
      return "table";
    case d_externalKind_memory:
      return "memory";
    case d_externalKind_global:
      return "global";
    case d_externalKind_tag:
      return "tag";
    default:
      return "unknown";
  }
}

static uint32_t xt_wasm_import_count(const M3Module *module) {
  uint32_t count = module->numFuncImports;
  for (uint32_t i = 0; i < module->numMemories; i++) {
    if (module->memories[i] != NULL && module->memories[i]->imported) count++;
  }
  for (uint32_t i = 0; i < module->numTables; i++) {
    if (module->tables[i] != NULL && module->tables[i]->imported) count++;
  }
  for (uint32_t i = 0; i < module->numGlobals; i++) {
    if (module->globals[i].import.moduleUtf8 != NULL ||
        module->globals[i].import.fieldUtf8 != NULL) {
      count++;
    }
  }
#if d_m3HasExceptionHandling || d_m3HasStackSwitching
  for (uint32_t i = 0; i < module->numTags; i++) {
    if (module->tags[i].imported) count++;
  }
#endif
  return count;
}

static int xt_wasm_collect_imports(wasm_module *out, const M3Module *module) {
  uint32_t count = xt_wasm_import_count(module);
  out->imports = count == 0 ? NULL : (wasm_import_desc *)calloc(count, sizeof(wasm_import_desc));
  if (count != 0 && out->imports == NULL) return 0;
  uint32_t at = 0;
  for (uint32_t i = 0; i < module->numFuncImports && at < count; i++) {
    const M3Function *function = &module->functions[i];
    if (function->import.moduleUtf8 == NULL && function->import.fieldUtf8 == NULL) continue;
    out->imports[at].module = xt_wasm_dup_range(function->import.moduleUtf8,
                                                xt_wasm_text_length(function->import.moduleUtf8));
    out->imports[at].name = xt_wasm_dup_range(function->import.fieldUtf8,
                                              xt_wasm_text_length(function->import.fieldUtf8));
    out->imports[at].name_length = xt_wasm_text_length(function->import.fieldUtf8);
    out->imports[at].kind = d_externalKind_function;
    at++;
  }
  for (uint32_t i = 0; i < module->numMemories && at < count; i++) {
    const M3Memory *memory = module->memories[i];
    if (memory == NULL || !memory->imported) continue;
    out->imports[at].module = xt_wasm_dup_range(memory->import.moduleUtf8,
                                                xt_wasm_text_length(memory->import.moduleUtf8));
    out->imports[at].name = xt_wasm_dup_range(memory->import.fieldUtf8,
                                              xt_wasm_text_length(memory->import.fieldUtf8));
    out->imports[at].name_length = xt_wasm_text_length(memory->import.fieldUtf8);
    out->imports[at].kind = d_externalKind_memory;
    at++;
  }
  for (uint32_t i = 0; i < module->numTables && at < count; i++) {
    const M3Table *table = module->tables[i];
    if (table == NULL || !table->imported) continue;
    out->imports[at].module = xt_wasm_dup_range(table->import.moduleUtf8,
                                                xt_wasm_text_length(table->import.moduleUtf8));
    out->imports[at].name = xt_wasm_dup_range(table->import.fieldUtf8,
                                              xt_wasm_text_length(table->import.fieldUtf8));
    out->imports[at].name_length = xt_wasm_text_length(table->import.fieldUtf8);
    out->imports[at].kind = d_externalKind_table;
    at++;
  }
  for (uint32_t i = 0; i < module->numGlobals && at < count; i++) {
    const M3Global *global = &module->globals[i];
    if (global->import.moduleUtf8 == NULL && global->import.fieldUtf8 == NULL) continue;
    out->imports[at].module = xt_wasm_dup_range(global->import.moduleUtf8,
                                                xt_wasm_text_length(global->import.moduleUtf8));
    out->imports[at].name = xt_wasm_dup_range(global->import.fieldUtf8,
                                              xt_wasm_text_length(global->import.fieldUtf8));
    out->imports[at].name_length = xt_wasm_text_length(global->import.fieldUtf8);
    out->imports[at].kind = d_externalKind_global;
    at++;
  }
#if d_m3HasExceptionHandling || d_m3HasStackSwitching
  for (uint32_t i = 0; i < module->numTags && at < count; i++) {
    const M3Tag *tag = &module->tags[i];
    if (!tag->imported) continue;
    out->imports[at].module = xt_wasm_dup_range(tag->import.moduleUtf8,
                                                xt_wasm_text_length(tag->import.moduleUtf8));
    out->imports[at].name = xt_wasm_dup_range(tag->import.fieldUtf8,
                                              xt_wasm_text_length(tag->import.fieldUtf8));
    out->imports[at].name_length = xt_wasm_text_length(tag->import.fieldUtf8);
    out->imports[at].kind = d_externalKind_tag;
    at++;
  }
#endif
  out->import_count = at;
  return 1;
}

static int xt_wasm_collect_exports(wasm_module *out, const M3Module *module) {
  out->export_count = module->numExports;
  out->exports = module->numExports == 0
                     ? NULL
                     : (wasm_export_desc *)calloc(module->numExports, sizeof(wasm_export_desc));
  if (module->numExports != 0 && out->exports == NULL) return 0;
  for (uint32_t i = 0; i < module->numExports; i++) {
    const M3Export *exported = &module->exports[i];
    out->exports[i].name = xt_wasm_dup_range(exported->name, exported->nameLength);
    out->exports[i].name_length = exported->nameLength;
    out->exports[i].kind = exported->kind;
    out->exports[i].index = exported->index;
  }
  return 1;
}

void xt_wasm_module_release(wasm_module *module) {
  if (module == NULL) return;
  for (uint32_t i = 0; i < module->export_count; i++) free(module->exports[i].name);
  for (uint32_t i = 0; i < module->import_count; i++) {
    free(module->imports[i].module);
    free(module->imports[i].name);
  }
  free(module->exports);
  free(module->imports);
  free(module->bytes);
  free(module);
}

wasm_module *xt_wasm_module_parse(const uint8_t *bytes, uint32_t length, char **error) {
  wasm_module *module = (wasm_module *)calloc(1, sizeof(wasm_module));
  if (module == NULL) {
    xt_wasm_set_error(error, "WebAssembly: out of memory");
    return NULL;
  }
  module->bytes = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (module->bytes == NULL) {
    xt_wasm_set_error(error, "WebAssembly: out of memory");
    free(module);
    return NULL;
  }
  memcpy(module->bytes, bytes, length);
  module->length = length;

  IM3Environment environment = m3_NewEnvironment();
  if (environment == NULL) {
    xt_wasm_set_error(error, "WebAssembly: could not create the interpreter environment");
    xt_wasm_module_release(module);
    return NULL;
  }
  IM3Module parsed = NULL;
  M3Result result = m3_ParseModule(environment, &parsed, module->bytes, length);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    m3_FreeEnvironment(environment);
    xt_wasm_module_release(module);
    return NULL;
  }
  int ok = xt_wasm_collect_exports(module, parsed) && xt_wasm_collect_imports(module, parsed);
  m3_FreeModule(parsed);
  m3_FreeEnvironment(environment);
  if (!ok) {
    xt_wasm_set_error(error, "WebAssembly: out of memory while reading module metadata");
    xt_wasm_module_release(module);
    return NULL;
  }
  return module;
}

wasm_module *xt_wasm_module_of(xt_value value) {
  if (!XT_IS_OBJECT(value)) return NULL;
  xt_value pointer = xt_get(value, xt_wasm_module_key());
  return (wasm_module *)xt_wasm_value_ptr(pointer);
}

xt_value xt_wasm_module_object(wasm_module *module, char **error) {
  (void)error;
  xt_value object = xt_wasm_object_new(XT_WASM_KIND_MODULE);
  xt_object_set(object, xt_wasm_module_key(), xt_wasm_ptr_value(module));
  return object;
}

xt_value xt_wasm_compile_to_module(xt_value value, char **error) {
  uint8_t *bytes = NULL;
  uint32_t length = 0;
  if (!xt_wasm_bytes_from_value(value, &bytes, &length, error)) return XT_UNDEFINED;
  wasm_module *module = xt_wasm_module_parse(bytes, length, error);
  free(bytes);
  if (module == NULL) return XT_UNDEFINED;
  return xt_wasm_module_object(module, error);
}

static xt_value xt_wasm_descriptor(const char *name, const char *kind) {
  xt_value descriptor = xt_object_new();
  xt_object_set(descriptor, xt_string_from_cstr("name"), xt_string_from_cstr(name));
  xt_object_set(descriptor, xt_string_from_cstr("kind"), xt_string_from_cstr(kind));
  return descriptor;
}

xt_value xt_wasm_module_exports_value(const wasm_module *module) {
  xt_value result = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < module->export_count; i++) {
    xt_value descriptor = xt_wasm_descriptor(module->exports[i].name,
                                             xt_wasm_export_kind_name(module->exports[i].kind));
    xt_array_push(result, descriptor);
  }
  return result;
}

xt_value xt_wasm_module_imports_value(const wasm_module *module) {
  xt_value result = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < module->import_count; i++) {
    /* The spec order is module, name, kind; keeping it also makes the value
     * comparable with Node's `JSON.stringify(WebAssembly.Module.imports(...))`. */
    xt_value descriptor = xt_object_new();
    xt_object_set(descriptor, xt_string_from_cstr("module"),
                  module->imports[i].module == NULL
                      ? XT_UNDEFINED
                      : xt_string_from_cstr(module->imports[i].module));
    xt_object_set(descriptor, xt_string_from_cstr("name"),
                  xt_string_from_cstr(module->imports[i].name));
    xt_object_set(descriptor, xt_string_from_cstr("kind"),
                  xt_string_from_cstr(xt_wasm_export_kind_name(module->imports[i].kind)));
    xt_array_push(result, descriptor);
  }
  return result;
}

const wasm_export_desc *xt_wasm_module_find_export(const wasm_module *module,
                                                   const char *name) {
  if (module == NULL || name == NULL) return NULL;
  for (uint32_t i = 0; i < module->export_count; i++) {
    if (strcmp(module->exports[i].name, name) == 0) return &module->exports[i];
  }
  return NULL;
}
