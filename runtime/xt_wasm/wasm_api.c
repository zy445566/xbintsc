/*
 * xbintsc runtime — the wasm3-backed `WebAssembly` namespace.
 *
 * This is the JavaScript surface: the `compile` / `instantiate` / `validate`
 * statics, the `Module` / `Instance` / `Memory` constructors and their shared
 * prototypes. The interpreter itself lives in `wasm_module.c` (parse and
 * metadata) and `wasm_instance.c` (linking and calls).
 */

#include "wasm.h"

#include <stdlib.h>
#include <string.h>

static xt_value xt_wasm_prototypes[XT_WASM_KIND_COUNT];
static xt_value xt_wasm_constructors[XT_WASM_KIND_COUNT];
static int xt_wasm_initialised = 0;

static xt_value xt_wasm_module_key_value = XT_UNDEFINED;
static xt_value xt_wasm_instance_key_value = XT_UNDEFINED;
static xt_value xt_wasm_memory_key_value = XT_UNDEFINED;

static const char *const xt_wasm_kind_names[XT_WASM_KIND_COUNT] = {
    "Module", "Instance", "Memory", "Table", "Global",
};

static xt_value xt_wasm_ctor_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                    xt_value *argv);

void xt_wasm_api_init(void) {
  if (xt_wasm_initialised) return;
  xt_wasm_initialised = 1;
  xt_gc_add_root_range(xt_wasm_prototypes, XT_WASM_KIND_COUNT);
  xt_gc_add_root_range(xt_wasm_constructors, XT_WASM_KIND_COUNT);
  for (int kind = 0; kind < XT_WASM_KIND_COUNT; kind++) {
    xt_value prototype = xt_object_new();
    xt_value name = xt_string_from_cstr(xt_wasm_kind_names[kind]);
    xt_value kindValue = xt_number((double)kind);
    xt_value constructor = xt_closure_new((void *)xt_wasm_ctor_invoke, 1, &kindValue);
    xt_function_set_prototype(constructor, prototype);
    xt_function_set_metadata(constructor, name, 1);
    xt_object_set(prototype, xt_string_from_cstr("constructor"), constructor);
    xt_wasm_prototypes[kind] = prototype;
    xt_wasm_constructors[kind] = constructor;
  }
}

xt_value xt_wasm_object_new(int kind) {
  xt_wasm_api_init();
  if (kind < 0 || kind >= XT_WASM_KIND_COUNT) return xt_object_new();
  return xt_object_new_with_proto(xt_wasm_prototypes[kind]);
}

xt_value xt_wasm_constructor(int kind) {
  xt_wasm_api_init();
  if (kind < 0 || kind >= XT_WASM_KIND_COUNT) return XT_UNDEFINED;
  return xt_wasm_constructors[kind];
}

static xt_value xt_wasm_cached_symbol(xt_value *slot, const char *name) {
  if (!xt_is_symbol(*slot)) {
    *slot = xt_symbol_new(xt_string_from_cstr(name));
    xt_gc_add_root(slot);
  }
  return *slot;
}

xt_value xt_wasm_module_key(void) {
  return xt_wasm_cached_symbol(&xt_wasm_module_key_value, "__wasmModule");
}

xt_value xt_wasm_instance_key(void) {
  return xt_wasm_cached_symbol(&xt_wasm_instance_key_value, "__wasmInstance");
}

xt_value xt_wasm_memory_key(void) {
  return xt_wasm_cached_symbol(&xt_wasm_memory_key_value, "__wasmMemory");
}

xt_value xt_wasm_ptr_value(const void *pointer) {
  return xt_number((double)(uintptr_t)pointer);
}

void *xt_wasm_value_ptr(xt_value value) {
  if (!XT_IS_NUMBER(value)) return NULL;
  return (void *)(uintptr_t)xt_to_double(value);
}

/* -- constructors --------------------------------------------------------- */

static xt_value xt_wasm_module_ctor(int32_t argc, xt_value *argv) {
  xt_value argument = xt_wasm_number_arg(argc, argv, 0);
  if (!xt_wasm_is_buffer_source(argument)) {
    xt_throw(xt_wasm_type_error("WebAssembly.Module: argument must be a BufferSource"));
    return XT_UNDEFINED;
  }
  char *error = NULL;
  xt_value module = xt_wasm_compile_to_module(argument, &error);
  if (module == XT_UNDEFINED) {
    xt_value thrown = xt_wasm_error("CompileError", error ? error : "invalid module");
    free(error);
    xt_throw(thrown);
    return XT_UNDEFINED;
  }
  free(error);
  return module;
}

static xt_value xt_wasm_instance_ctor(int32_t argc, xt_value *argv) {
  xt_value module = xt_wasm_number_arg(argc, argv, 0);
  if (xt_wasm_module_of(module) == NULL) {
    xt_throw(xt_wasm_type_error("WebAssembly.Instance: first argument must be a Module"));
    return XT_UNDEFINED;
  }
  char *error = NULL;
  xt_value instance = xt_wasm_instantiate_value(module, xt_wasm_number_arg(argc, argv, 1), &error);
  if (instance == XT_UNDEFINED) {
    xt_value thrown = xt_wasm_error("LinkError", error ? error : "could not instantiate module");
    free(error);
    xt_throw(thrown);
    return XT_UNDEFINED;
  }
  free(error);
  return instance;
}

/* A standalone `new WebAssembly.Memory({ initial, maximum })`. It owns an
 * ArrayBuffer rather than wasm3 memory: it cannot be imported by a module (the
 * bridge only links function imports), but its `.buffer` and `.grow()` behave
 * like the platform object. */
static uint32_t xt_wasm_page_count(xt_value descriptor, const char *key,
                                   uint32_t fallback) {
  xt_value value = xt_get(descriptor, xt_string_from_cstr(key));
  if (value == XT_UNDEFINED) return fallback;
  double count = xt_to_number(value);
  if (!(count >= 0) || count > 65536) return fallback;
  return (uint32_t)count;
}

static xt_value xt_wasm_standalone_memory_grow(xt_value thisValue, xt_value env,
                                               int32_t argc, xt_value *argv) {
  (void)env;
  uint32_t delta = argc > 0 ? (uint32_t)xt_to_int32(argv[0]) : 0;
  xt_value oldBuffer = xt_get(thisValue, xt_string_from_cstr("buffer"));
  uint32_t oldBytes = xt_is_array_buffer(oldBuffer) ? xt_array_buffer_byte_length(oldBuffer) : 0;
  uint32_t newBytes = oldBytes + delta * 65536u;
  xt_value newBuffer = xt_array_buffer_alloc(newBytes);
  if (xt_is_array_buffer(oldBuffer)) {
    uint8_t *scratch = (uint8_t *)malloc(oldBytes == 0 ? 1 : oldBytes);
    if (scratch != NULL) {
      uint32_t copied = xt_array_buffer_copy_bytes_out(oldBuffer, 0, scratch, oldBytes);
      xt_array_buffer_copy_bytes_in(newBuffer, 0, scratch, copied);
      free(scratch);
    }
  }
  xt_object_set(thisValue, xt_string_from_cstr("buffer"), newBuffer);
  return xt_number((double)(oldBytes / 65536u));
}

static xt_value xt_wasm_memory_ctor(int32_t argc, xt_value *argv) {
  xt_value descriptor = xt_wasm_object_arg(argc, argv, 0);
  if (descriptor == XT_UNDEFINED) {
    xt_throw(xt_wasm_type_error("WebAssembly.Memory: descriptor must be an object"));
    return XT_UNDEFINED;
  }
  uint32_t initial = xt_wasm_page_count(descriptor, "initial", 0);
  uint32_t maximum = xt_wasm_page_count(descriptor, "maximum", 0);
  if (maximum != 0 && initial > maximum) {
    xt_throw(xt_wasm_error("RangeError", "initial memory is larger than maximum"));
    return XT_UNDEFINED;
  }
  xt_value memory = xt_wasm_object_new(XT_WASM_KIND_MEMORY);
  xt_value buffer = xt_array_buffer_alloc(initial * 65536u);
  xt_object_set(memory, xt_string_from_cstr("buffer"), buffer);
  xt_object_set(memory, xt_string_from_cstr("grow"),
                xt_closure_new((void *)xt_wasm_standalone_memory_grow, 0, NULL));
  return memory;
}

static xt_value xt_wasm_ctor_invoke(xt_value thisValue, xt_value env, int32_t argc,
                                    xt_value *argv) {
  (void)thisValue;
  int kind = (int)xt_to_number(xt_closure_env(env, 0));
  switch (kind) {
    case XT_WASM_KIND_MODULE:
      return xt_wasm_module_ctor(argc, argv);
    case XT_WASM_KIND_INSTANCE:
      return xt_wasm_instance_ctor(argc, argv);
    case XT_WASM_KIND_MEMORY:
      return xt_wasm_memory_ctor(argc, argv);
    default:
      xt_throw(xt_wasm_type_error("WebAssembly: this constructor is not supported in this build"));
      return XT_UNDEFINED;
  }
}

/* -- static helpers ------------------------------------------------------- */

static xt_value xt_wasm_static_closure(xt_value thisValue, xt_value env, int32_t argc,
                                       xt_value *argv) {
  (void)thisValue;
  return xt_wasm_dispatch_static(xt_closure_env(env, 0), argc, argv);
}

static xt_value xt_wasm_module_exports_static(xt_value thisValue, xt_value env,
                                              int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value argument = xt_wasm_number_arg(argc, argv, 0);
  wasm_module *module = xt_wasm_module_of(argument);
  if (module == NULL) {
    xt_throw(xt_wasm_type_error("WebAssembly.Module.exports: argument must be a Module"));
    return XT_UNDEFINED;
  }
  return xt_wasm_module_exports_value(module);
}

static xt_value xt_wasm_module_imports_static(xt_value thisValue, xt_value env,
                                              int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  xt_value argument = xt_wasm_number_arg(argc, argv, 0);
  wasm_module *module = xt_wasm_module_of(argument);
  if (module == NULL) {
    xt_throw(xt_wasm_type_error("WebAssembly.Module.imports: argument must be a Module"));
    return XT_UNDEFINED;
  }
  return xt_wasm_module_imports_value(module);
}

static xt_value xt_wasm_module_custom_sections(xt_value thisValue, xt_value env,
                                               int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_array_new(0, NULL);
}

static xt_value xt_wasm_validate(xt_value argument) {
  if (!xt_wasm_is_buffer_source(argument)) {
    xt_throw(xt_wasm_type_error("WebAssembly.validate: argument must be a BufferSource"));
    return XT_UNDEFINED;
  }
  uint8_t *bytes = NULL;
  uint32_t length = 0;
  char *error = NULL;
  if (!xt_wasm_bytes_from_value(argument, &bytes, &length, &error)) {
    free(error);
    return XT_FALSE;
  }
  wasm_module *module = xt_wasm_module_parse(bytes, length, &error);
  free(bytes);
  free(error);
  if (module == NULL) return XT_FALSE;
  xt_wasm_module_release(module);
  return XT_TRUE;
}

xt_value xt_wasm_dispatch_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *method = xt_string_data(name);
  if (method == NULL) return XT_UNDEFINED;
  if (strcmp(method, "validate") == 0) {
    return xt_wasm_validate(xt_wasm_number_arg(argc, argv, 0));
  }
  if (strcmp(method, "compile") == 0) {
    xt_value argument = xt_wasm_number_arg(argc, argv, 0);
    if (!xt_wasm_is_buffer_source(argument)) {
      return xt_promise_reject(
          xt_wasm_type_error("WebAssembly.compile: argument must be a BufferSource"));
    }
    char *error = NULL;
    xt_value module = xt_wasm_compile_to_module(argument, &error);
    if (module == XT_UNDEFINED) {
      xt_value rejected = xt_wasm_error("CompileError", error ? error : "invalid module");
      free(error);
      return xt_promise_reject(rejected);
    }
    free(error);
    return xt_promise_resolve(module);
  }
  if (strcmp(method, "instantiate") == 0) {
    xt_value argument = xt_wasm_number_arg(argc, argv, 0);
    xt_value imports = xt_wasm_number_arg(argc, argv, 1);
    wasm_module *existing = xt_wasm_module_of(argument);
    xt_value module = argument;
    int fromBytes = 0;
    char *error = NULL;
    if (existing == NULL) {
      if (!xt_wasm_is_buffer_source(argument)) {
        return xt_promise_reject(
            xt_wasm_type_error("WebAssembly.instantiate: argument must be a Module or BufferSource"));
      }
      module = xt_wasm_compile_to_module(argument, &error);
      if (module == XT_UNDEFINED) {
        xt_value rejected = xt_wasm_error("CompileError", error ? error : "invalid module");
        free(error);
        return xt_promise_reject(rejected);
      }
      fromBytes = 1;
    }
    xt_value instance = xt_wasm_instantiate_value(module, imports, &error);
    if (instance == XT_UNDEFINED) {
      xt_value rejected = xt_wasm_error("LinkError", error ? error : "could not instantiate module");
      free(error);
      return xt_promise_reject(rejected);
    }
    free(error);
    if (!fromBytes) return xt_promise_resolve(instance);
    xt_value result = xt_object_new();
    xt_object_set(result, xt_string_from_cstr("module"), module);
    xt_object_set(result, xt_string_from_cstr("instance"), instance);
    return xt_promise_resolve(result);
  }
  return XT_UNDEFINED;
}

xt_value xt_wasm_build_object(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_wasm_api_init();
  xt_value object = xt_object_new();
  static const char *const statics[] = {"compile", "instantiate", "validate"};
  for (size_t i = 0; i < sizeof(statics) / sizeof(statics[0]); i++) {
    xt_value environment[1];
    environment[0] = xt_string_from_cstr(statics[i]);
    xt_object_set(object, environment[0],
                  xt_closure_new((void *)xt_wasm_static_closure, 1, environment));
  }
  static const struct {
    const char *name;
    int kind;
  } constructors[] = {
      {"Module", XT_WASM_KIND_MODULE},
      {"Instance", XT_WASM_KIND_INSTANCE},
      {"Memory", XT_WASM_KIND_MEMORY},
      {"Table", XT_WASM_KIND_TABLE},
      {"Global", XT_WASM_KIND_GLOBAL},
  };
  for (size_t i = 0; i < sizeof(constructors) / sizeof(constructors[0]); i++) {
    xt_object_set(object, xt_string_from_cstr(constructors[i].name),
                  xt_wasm_constructor(constructors[i].kind));
  }
  static const char *const errors[] = {"CompileError", "LinkError", "RuntimeError"};
  for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
    xt_object_set(object, xt_string_from_cstr(errors[i]),
                  xt_error_constructor(xt_string_from_cstr(errors[i])));
  }
  xt_value moduleConstructor = xt_wasm_constructor(XT_WASM_KIND_MODULE);
  xt_object_set(moduleConstructor, xt_string_from_cstr("exports"),
                xt_closure_new((void *)xt_wasm_module_exports_static, 0, NULL));
  xt_object_set(moduleConstructor, xt_string_from_cstr("imports"),
                xt_closure_new((void *)xt_wasm_module_imports_static, 0, NULL));
  xt_object_set(moduleConstructor, xt_string_from_cstr("customSections"),
                xt_closure_new((void *)xt_wasm_module_custom_sections, 0, NULL));
  return object;
}
