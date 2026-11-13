/*
 * xbintsc runtime — WebAssembly instances, imports and calls.
 *
 * wasm3 compiles lazily and exposes raw host functions with a fixed C
 * signature. This file resolves the JS import object into one `wasm_import`
 * record per function, bridges calls in both directions, and wraps exported
 * functions and memories as ordinary JS values.
 */

#include "wasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wasm_instance *xt_wasm_instances = NULL;
static int xt_wasm_roots_registered = 0;

static void xt_wasm_mark_roots(void) {
  for (wasm_instance *instance = xt_wasm_instances; instance != NULL;
       instance = instance->next) {
    xt_gc_mark_value(instance->object);
    xt_gc_mark_value(instance->exports);
    xt_gc_mark_value(instance->imports);
    xt_gc_mark_value(instance->pending_exception);
    for (wasm_import *entry = instance->import_functions; entry != NULL; entry = entry->next) {
      xt_gc_mark_value(entry->function);
    }
    for (wasm_memory *memory = instance->memories; memory != NULL; memory = memory->next) {
      xt_gc_mark_value(memory->object);
      xt_gc_mark_value(memory->buffer);
    }
  }
}

static void xt_wasm_register_roots(void) {
  if (xt_wasm_roots_registered) return;
  xt_wasm_roots_registered = 1;
  xt_gc_register_root_provider(xt_wasm_mark_roots);
}

static void xt_wasm_track_instance(wasm_instance *instance) {
  xt_wasm_register_roots();
  instance->next = xt_wasm_instances;
  xt_wasm_instances = instance;
}

static wasm_instance *xt_wasm_instance_of(xt_value value) {
  if (!XT_IS_OBJECT(value)) return NULL;
  return (wasm_instance *)xt_wasm_value_ptr(xt_get(value, xt_wasm_instance_key()));
}

int xt_wasm_is_instance(xt_value value) { return xt_wasm_instance_of(value) != NULL; }

uint32_t xt_wasm_memory_index(wasm_memory *memory) {
  return memory == NULL ? 0 : memory->index;
}

const char *xt_wasm_memory_name(wasm_memory *memory) {
  (void)memory;
  return "memory";
}

static uint32_t xt_wasm_pages_for_bytes(uint32_t bytes) {
  return (bytes + 65535u) / 65536u;
}

xt_value xt_wasm_memory_buffer(wasm_memory *memory) {
  if (memory == NULL) return xt_undefined();
  wasm_instance *instance = memory->instance;
  size_t size = m3_GetMemorySize(instance->module, memory->index);
  if (size > 0xFFFFFFFFu) size = 0xFFFFFFFFu;
  uint32_t bytes = (uint32_t)size;
  if (!xt_is_array_buffer(memory->buffer) || memory->buffer_bytes != bytes) {
    xt_value buffer = xt_array_buffer_alloc(bytes);
    uint8_t *source = m3_GetMemory(instance->module, &size, memory->index);
    if (source != NULL) xt_array_buffer_copy_bytes_in(buffer, 0, source, bytes);
    memory->buffer = buffer;
    memory->buffer_bytes = bytes;
  } else {
    uint8_t *source = m3_GetMemory(instance->module, &size, memory->index);
    if (source != NULL) xt_array_buffer_copy_bytes_in(memory->buffer, 0, source, bytes);
  }
  return memory->buffer;
}

void xt_wasm_sync_memories(wasm_instance *instance, int into_wasm) {
  if (instance == NULL) return;
  for (wasm_memory *memory = instance->memories; memory != NULL; memory = memory->next) {
    if (!xt_is_array_buffer(memory->buffer)) continue;
    size_t size = m3_GetMemorySize(instance->module, memory->index);
    if (size > 0xFFFFFFFFu) continue;
    uint32_t bufferBytes = xt_array_buffer_byte_length(memory->buffer);
    if (into_wasm && bufferBytes > (uint32_t)size) {
      char *error = NULL;
      xt_wasm_memory_grow(memory, xt_wasm_pages_for_bytes(bufferBytes) -
                                      xt_wasm_pages_for_bytes((uint32_t)size),
                          &error);
      free(error);
      size = m3_GetMemorySize(instance->module, memory->index);
    }
    uint8_t *destination = m3_GetMemory(instance->module, &size, memory->index);
    if (destination == NULL) continue;
    if (into_wasm) {
      uint32_t count = bufferBytes < (uint32_t)size ? bufferBytes : (uint32_t)size;
      xt_array_buffer_copy_bytes_out(memory->buffer, 0, destination, count);
    } else if (size <= 0xFFFFFFFFu) {
      if (memory->buffer_bytes != (uint32_t)size ||
          xt_array_buffer_byte_length(memory->buffer) != (uint32_t)size) {
        memory->buffer = xt_array_buffer_alloc((uint32_t)size);
        memory->buffer_bytes = (uint32_t)size;
      }
      xt_array_buffer_copy_bytes_in(memory->buffer, 0, destination, (uint32_t)size);
    }
  }
}

void xt_wasm_memory_grow(wasm_memory *memory, uint32_t delta_pages, char **error) {
  if (memory == NULL) return;
  wasm_instance *instance = memory->instance;
  IM3Memory wasmMemory = instance->module->memories[memory->index];
  if (wasmMemory == NULL) {
    xt_wasm_set_error(error, "WebAssembly.Memory: memory is not available");
    return;
  }
  uint64_t pages = wasmMemory->numPages + (uint64_t)delta_pages;
  M3Result result = ResizeMemory(instance->runtime, wasmMemory, pages);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    return;
  }
  memory->buffer = XT_UNDEFINED;
  memory->buffer_bytes = 0;
  (void)xt_wasm_memory_buffer(memory);
}

static xt_value xt_wasm_memory_buffer_get(xt_value thisValue, xt_value env, int32_t argc,
                                          xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  return xt_wasm_memory_buffer((wasm_memory *)xt_wasm_value_ptr(xt_closure_env(env, 0)));
}

static xt_value xt_wasm_memory_grow_method(xt_value thisValue, xt_value env, int32_t argc,
                                           xt_value *argv) {
  (void)thisValue;
  wasm_memory *memory = (wasm_memory *)xt_wasm_value_ptr(xt_closure_env(env, 0));
  uint32_t delta = argc > 0 ? (uint32_t)xt_to_int32(argv[0]) : 0;
  uint32_t oldPages = memory == NULL ? 0 : (uint32_t)(memory->instance->module
                                                          ->memories[memory->index]
                                                          ->numPages);
  char *error = NULL;
  xt_wasm_memory_grow(memory, delta, &error);
  if (error != NULL) {
    xt_value thrown = xt_wasm_error("RangeError", error);
    free(error);
    xt_throw(thrown);
    return XT_UNDEFINED;
  }
  return xt_number((double)oldPages);
}

int xt_wasm_memory_pointer(xt_value object, wasm_memory **out) {
  if (!XT_IS_OBJECT(object)) return 0;
  *out = (wasm_memory *)xt_wasm_value_ptr(xt_get(object, xt_wasm_memory_key()));
  return *out != NULL;
}

static wasm_memory *xt_wasm_memory_create(wasm_instance *instance, uint32_t index) {
  wasm_memory *memory = (wasm_memory *)calloc(1, sizeof(wasm_memory));
  if (memory == NULL) return NULL;
  memory->instance = instance;
  memory->index = index;
  memory->object = xt_wasm_object_new(XT_WASM_KIND_MEMORY);
  memory->buffer = XT_UNDEFINED;
  xt_object_set(memory->object, xt_wasm_memory_key(), xt_wasm_ptr_value(memory));
  xt_value environment[1];
  environment[0] = xt_wasm_ptr_value(memory);
  xt_object_define_getter(memory->object, xt_string_from_cstr("buffer"),
                          xt_closure_new((void *)xt_wasm_memory_buffer_get, 1, environment));
  xt_object_set(memory->object, xt_string_from_cstr("grow"),
                xt_closure_new((void *)xt_wasm_memory_grow_method, 1, environment));
  memory->next = instance->memories;
  instance->memories = memory;
  return memory;
}

/* -- imported-function bridge -------------------------------------------- */

union xt_wasm_slot {
  uint32_t i32;
  uint64_t i64;
  float f32;
  double f64;
  uint64_t raw;
};

static xt_value xt_wasm_slot_to_value(uint64_t slot, M3ValueType type) {
  union xt_wasm_slot value;
  value.raw = slot;
  switch (type) {
    case c_m3Type_i32:
      return xt_number((double)(int32_t)value.i32);
    case c_m3Type_i64:
      return xt_wasm_from_i64((int64_t)value.i64);
    case c_m3Type_f32:
      return xt_number((double)value.f32);
    case c_m3Type_f64:
      return xt_number(value.f64);
    default:
      return XT_UNDEFINED;
  }
}

static int xt_wasm_value_to_slot(xt_value value, M3ValueType type, uint64_t *slot) {
  union xt_wasm_slot converted;
  converted.raw = 0;
  switch (type) {
    case c_m3Type_i32:
      converted.i32 = (uint32_t)xt_to_int32(value);
      break;
    case c_m3Type_i64:
      converted.i64 = (uint64_t)xt_wasm_to_i64(value);
      break;
    case c_m3Type_f32:
      converted.f32 = (float)xt_to_number(value);
      break;
    case c_m3Type_f64:
      converted.f64 = xt_to_number(value);
      break;
    default:
      return 0;
  }
  *slot = converted.raw;
  return 1;
}

static char *xt_wasm_function_signature(IM3Function function) {
  uint32_t returns = m3_GetRetCount(function);
  uint32_t arguments = m3_GetArgCount(function);
  char *signature = (char *)malloc((size_t)returns + arguments + 3);
  if (signature == NULL) return NULL;
  char *cursor = signature;
  for (uint32_t i = 0; i < returns; i++) {
    switch (m3_GetRetType(function, i)) {
      case c_m3Type_i32:
        *cursor++ = 'i';
        break;
      case c_m3Type_i64:
        *cursor++ = 'I';
        break;
      case c_m3Type_f32:
        *cursor++ = 'f';
        break;
      case c_m3Type_f64:
        *cursor++ = 'F';
        break;
      default:
        *cursor++ = 'v';
        break;
    }
  }
  *cursor++ = '(';
  for (uint32_t i = 0; i < arguments; i++) {
    switch (m3_GetArgType(function, i)) {
      case c_m3Type_i32:
        *cursor++ = 'i';
        break;
      case c_m3Type_i64:
        *cursor++ = 'I';
        break;
      case c_m3Type_f32:
        *cursor++ = 'f';
        break;
      case c_m3Type_f64:
        *cursor++ = 'F';
        break;
      default:
        *cursor++ = 'i';
        break;
    }
  }
  *cursor++ = ')';
  *cursor = '\0';
  return signature;
}

static const void *xt_wasm_import_bridge(IM3Runtime runtime, IM3ImportContext context,
                                        uint64_t *stack, void *memory) {
  (void)runtime;
  (void)memory;
  wasm_import *entry = (wasm_import *)context->userdata;
  if (entry == NULL) return m3Err_trapUnreachable;
  uint32_t returns = m3_GetRetCount(context->function);
  uint32_t arguments = m3_GetArgCount(context->function);
  /* Build the arguments in a GC array: a plain malloc buffer is invisible to
   * the collector, and the callback can allocate (and therefore collect)
   * before it reads them. The array itself stays reachable from the C stack. */
  xt_value argumentArray = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < arguments; i++) {
    xt_array_push(argumentArray, xt_wasm_slot_to_value(stack[returns + i],
                                                      m3_GetArgType(context->function, i)));
  }
  xt_value *values = arguments == 0 ? NULL : xt_array_items(argumentArray);

  xt_value result = XT_UNDEFINED;
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    result = xt_closure_call(entry->function, (int32_t)arguments, values);
    xt_try_leave(frame);
  } else {
    entry->instance->pending_exception = xt_try_exception(frame);
    xt_try_leave(frame);
    return m3Err_trapUnreachable;
  }

  if (returns == 1) {
    if (!xt_wasm_value_to_slot(result, m3_GetRetType(context->function, 0), &stack[0])) {
      return m3Err_trapUnreachable;
    }
  } else if (returns > 1) {
    xt_array *array = XT_IS_ARRAY(result) ? xt_as_array(result) : NULL;
    for (uint32_t i = 0; i < returns; i++) {
      xt_value item = array != NULL && i < array->length ? array->items[i] : XT_UNDEFINED;
      if (!xt_wasm_value_to_slot(item, m3_GetRetType(context->function, i), &stack[i])) {
        return m3Err_trapUnreachable;
      }
    }
  }
  return m3Err_none;
}

static int xt_wasm_link_functions(wasm_instance *instance, xt_value imports, char **error) {
  IM3Module module = instance->module;
  if (module->numFuncImports != 0 && !XT_IS_OBJECT(imports) && !XT_IS_ARRAY(imports) &&
      !XT_IS_FUNCTION(imports)) {
    xt_wasm_set_error(error, "WebAssembly: missing import object");
    return 0;
  }
  for (uint32_t i = 0; i < module->numFuncImports; i++) {
    IM3Function function = &module->functions[i];
    const char *moduleName = function->import.moduleUtf8;
    const char *fieldName = function->import.fieldUtf8;
    if (moduleName == NULL || fieldName == NULL) continue;
    xt_value namespaceValue = xt_get(imports, xt_string_from_cstr(moduleName));
    xt_value imported = XT_IS_OBJECT(namespaceValue) || XT_IS_ARRAY(namespaceValue) ||
                                XT_IS_FUNCTION(namespaceValue)
                            ? xt_get(namespaceValue, xt_string_from_cstr(fieldName))
                            : XT_UNDEFINED;
    if (!XT_IS_FUNCTION(imported)) {
      xt_wasm_set_error(error, "WebAssembly: import '%s.%s' is not a function", moduleName,
                        fieldName);
      return 0;
    }
    wasm_import *entry = (wasm_import *)calloc(1, sizeof(wasm_import));
    if (entry == NULL) {
      xt_wasm_set_error(error, "WebAssembly: out of memory");
      return 0;
    }
    entry->instance = instance;
    entry->function = imported;
    entry->function_index = i;
    char *signature = xt_wasm_function_signature(function);
    if (signature == NULL) {
      free(entry);
      xt_wasm_set_error(error, "WebAssembly: out of memory");
      return 0;
    }
    M3Result result = m3_LinkRawFunctionEx(module, moduleName, fieldName, signature,
                                           xt_wasm_import_bridge, entry);
    free(signature);
    if (result != NULL) {
      free(entry);
      xt_wasm_set_error(error, "WebAssembly: %s", result);
      return 0;
    }
    entry->next = instance->import_functions;
    instance->import_functions = entry;
  }
  return 1;
}

static int xt_wasm_check_unsupported_imports(wasm_instance *instance, char **error) {
  IM3Module module = instance->module;
  for (uint32_t i = 0; i < module->numMemories; i++) {
    if (module->memories[i] != NULL && module->memories[i]->imported) {
      xt_wasm_set_error(error,
                        "WebAssembly: imported memory '%s.%s' is not supported; only "
                        "function imports are implemented",
                        module->memories[i]->import.moduleUtf8,
                        module->memories[i]->import.fieldUtf8);
      return 0;
    }
  }
  for (uint32_t i = 0; i < module->numTables; i++) {
    if (module->tables[i] != NULL && module->tables[i]->imported) {
      xt_wasm_set_error(error, "WebAssembly: imported tables are not supported in this build");
      return 0;
    }
  }
  for (uint32_t i = 0; i < module->numGlobals; i++) {
    if (module->globals[i].import.moduleUtf8 != NULL) {
      xt_wasm_set_error(error, "WebAssembly: imported globals are not supported in this build");
      return 0;
    }
  }
  return 1;
}

/* -- exported functions --------------------------------------------------- */

static xt_value xt_wasm_export_call(xt_value thisValue, xt_value env, int32_t argc,
                                    xt_value *argv) {
  (void)thisValue;
  wasm_instance *instance = (wasm_instance *)xt_wasm_value_ptr(xt_closure_env(env, 0));
  IM3Function function = (IM3Function)xt_wasm_value_ptr(xt_closure_env(env, 1));
  if (instance == NULL || function == NULL) return XT_UNDEFINED;
  uint32_t wanted = m3_GetArgCount(function);
  if ((uint32_t)argc != wanted) {
    char message[128];
    snprintf(message, sizeof(message), "WebAssembly: expected %u arguments, got %d", wanted, argc);
    xt_throw(xt_wasm_type_error(message));
    return XT_UNDEFINED;
  }
  uint32_t returns = m3_GetRetCount(function);
  union xt_wasm_slot *arguments =
      wanted == 0 ? NULL : (union xt_wasm_slot *)calloc(wanted, sizeof(union xt_wasm_slot));
  union xt_wasm_slot *results =
      returns == 0 ? NULL : (union xt_wasm_slot *)calloc(returns, sizeof(union xt_wasm_slot));
  const void **argumentPointers =
      wanted == 0 ? NULL : (const void **)calloc(wanted, sizeof(const void *));
  const void **resultPointers =
      returns == 0 ? NULL : (const void **)calloc(returns, sizeof(const void *));
  if ((wanted != 0 && (arguments == NULL || argumentPointers == NULL)) ||
      (returns != 0 && (results == NULL || resultPointers == NULL))) {
    free(arguments);
    free(results);
    free(argumentPointers);
    free(resultPointers);
    xt_throw(xt_error_from_message("RangeError: WebAssembly call is too large"));
    return XT_UNDEFINED;
  }
  for (uint32_t i = 0; i < wanted; i++) {
    if (!xt_wasm_value_to_slot(argv[i], m3_GetArgType(function, i), &arguments[i].raw)) {
      free(arguments);
      free(results);
      free(argumentPointers);
      free(resultPointers);
      xt_throw(xt_wasm_type_error("WebAssembly: unsupported argument type"));
      return XT_UNDEFINED;
    }
    argumentPointers[i] = &arguments[i].raw;
  }
  for (uint32_t i = 0; i < returns; i++) resultPointers[i] = &results[i].raw;

  xt_wasm_sync_memories(instance, 1);
  M3Result result = m3_Call(function, wanted, argumentPointers);
  if (result == NULL && returns != 0) {
    result = m3_GetResults(function, returns, resultPointers);
  }
  xt_wasm_sync_memories(instance, 0);
  free(arguments);
  free(argumentPointers);

  if (!XT_IS_UNDEFINED(instance->pending_exception)) {
    xt_value thrown = instance->pending_exception;
    instance->pending_exception = XT_UNDEFINED;
    free(results);
    free(resultPointers);
    xt_throw(thrown);
    return XT_UNDEFINED;
  }
  if (result != NULL) {
    free(results);
    free(resultPointers);
    xt_throw(xt_wasm_error("RuntimeError", result));
    return XT_UNDEFINED;
  }
  if (returns == 0) {
    free(results);
    free(resultPointers);
    return XT_UNDEFINED;
  }
  if (returns == 1) {
    xt_value value = xt_wasm_slot_to_value(results[0].raw, m3_GetRetType(function, 0));
    free(results);
    free(resultPointers);
    return value;
  }
  xt_value values = xt_array_new(0, NULL);
  for (uint32_t i = 0; i < returns; i++) {
    xt_array_push(values, xt_wasm_slot_to_value(results[i].raw, m3_GetRetType(function, i)));
  }
  free(results);
  free(resultPointers);
  return values;
}

/* -- instance construction ------------------------------------------------ */

static xt_value xt_wasm_build_exports(wasm_instance *instance, char **error) {
  xt_value exports = xt_object_new();
  IM3Module module = instance->module;
  for (uint32_t i = 0; i < module->numExports; i++) {
    const M3Export *exported = &module->exports[i];
    xt_value name = xt_string_new(exported->name, exported->nameLength);
    if (exported->kind == d_externalKind_function) {
      if (exported->index >= module->numFunctions) continue;
      xt_value environment[2];
      environment[0] = xt_wasm_ptr_value(instance);
      environment[1] = xt_wasm_ptr_value(&module->functions[exported->index]);
      xt_object_set(exports, name,
                    xt_closure_new((void *)xt_wasm_export_call, 2, environment));
    } else if (exported->kind == d_externalKind_memory) {
      wasm_memory *memory = xt_wasm_memory_create(instance, exported->index);
      if (memory == NULL) {
        xt_wasm_set_error(error, "WebAssembly: out of memory");
        return XT_UNDEFINED;
      }
      xt_object_set(exports, name, memory->object);
    }
  }
  return exports;
}

xt_value xt_wasm_instance_value(wasm_module *source, xt_value imports, char **error) {
  wasm_instance *instance = (wasm_instance *)calloc(1, sizeof(wasm_instance));
  if (instance == NULL) {
    xt_wasm_set_error(error, "WebAssembly: out of memory");
    return XT_UNDEFINED;
  }
  instance->source = source;
  instance->imports = imports;
  instance->pending_exception = XT_UNDEFINED;
  instance->object = xt_wasm_object_new(XT_WASM_KIND_INSTANCE);
  instance->exports = xt_object_new();

  instance->environment = m3_NewEnvironment();
  if (instance->environment == NULL) {
    xt_wasm_set_error(error, "WebAssembly: could not create the interpreter environment");
    return XT_UNDEFINED;
  }
  instance->runtime = m3_NewRuntime(instance->environment, 256 * 1024, instance);
  if (instance->runtime == NULL) {
    xt_wasm_set_error(error, "WebAssembly: could not create an interpreter runtime");
    return XT_UNDEFINED;
  }
  M3Result result = m3_ParseModule(instance->environment, &instance->module,
                                   source->bytes, source->length);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    return XT_UNDEFINED;
  }
  /* m3_LoadModule transfers ownership of the module to the runtime, even when
   * it reports an error, so it must not be freed by this function. */
  result = m3_LoadModule(instance->runtime, instance->module);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    return XT_UNDEFINED;
  }
  if (!xt_wasm_check_unsupported_imports(instance, error)) return XT_UNDEFINED;
  if (!xt_wasm_link_functions(instance, imports, error)) return XT_UNDEFINED;
  result = m3_CompileModule(instance->module);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    return XT_UNDEFINED;
  }
  result = m3_RunStart(instance->module);
  if (result != NULL) {
    xt_wasm_set_error(error, "%s", result);
    return XT_UNDEFINED;
  }
  xt_value exports = xt_wasm_build_exports(instance, error);
  if (exports == XT_UNDEFINED) return XT_UNDEFINED;
  instance->exports = exports;
  xt_object_set(instance->object, xt_string_from_cstr("exports"), exports);
  xt_object_set(instance->object, xt_wasm_instance_key(), xt_wasm_ptr_value(instance));
  xt_wasm_track_instance(instance);
  return instance->object;
}

xt_value xt_wasm_instantiate_value(xt_value module_or_bytes, xt_value imports,
                                   char **error) {
  wasm_module *module = xt_wasm_module_of(module_or_bytes);
  if (module == NULL) {
    xt_wasm_set_error(error, "WebAssembly: first argument must be a Module");
    return XT_UNDEFINED;
  }
  return xt_wasm_instance_value(module, imports, error);
}
