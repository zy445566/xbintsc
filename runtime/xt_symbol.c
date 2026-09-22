/*
 * xbintsc runtime — Symbol primitives.
 *
 * Symbols are heap objects (`XT_OBJECT_KIND_SYMBOL`) tagged as `XT_TAG_OBJECT`
 * so they can travel through the generic value plumbing, but they are
 * distinguished by their header kind. Property keys are either strings or
 * symbols; see `xt_to_property_key` in `xt_containers.c`.
 */

#include "rt_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  xt_header header;
  /* `XT_UNDEFINED` for `Symbol()` without a description, otherwise a string. */
  xt_value description;
  uint64_t id;
} xt_symbol_data;

static uint64_t g_symbol_counter = 0;

int xt_is_symbol(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return ((xt_header *)XT_GET_PTR(value))->kind == XT_OBJECT_KIND_SYMBOL;
}

static xt_symbol_data *xt_as_symbol(xt_value value) {
  if (!xt_is_symbol(value)) return NULL;
  return (xt_symbol_data *)XT_GET_PTR(value);
}

xt_value xt_symbol_new(xt_value description) {
  xt_symbol_data *symbol =
      (xt_symbol_data *)xt_alloc(sizeof(xt_symbol_data), XT_OBJECT_KIND_SYMBOL);
  symbol->description = description;
  symbol->id = ++g_symbol_counter;
  return XT_FROM_PTR(XT_TAG_OBJECT, symbol);
}

/* -- well-known symbols --------------------------------------------------- */

static const char *const XT_WELL_KNOWN_NAMES[] = {
    "asyncIterator", "hasInstance",   "isConcatSpreadable", "iterator",
    "match",         "matchAll",      "replace",            "search",
    "species",       "split",         "toPrimitive",        "toStringTag",
    "unscopables",
};
#define XT_WELL_KNOWN_COUNT \
  ((int)(sizeof(XT_WELL_KNOWN_NAMES) / sizeof(XT_WELL_KNOWN_NAMES[0])))

static xt_value g_well_known[XT_WELL_KNOWN_COUNT];
static int g_well_known_ready = 0;

static void xt_symbol_init_well_known(void) {
  if (g_well_known_ready) return;
  g_well_known_ready = 1;
  for (int i = 0; i < XT_WELL_KNOWN_COUNT; i++) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "Symbol.%s", XT_WELL_KNOWN_NAMES[i]);
    g_well_known[i] = xt_symbol_new(xt_string_from_cstr(buffer));
  }
}

xt_value xt_symbol_well_known(const char *name) {
  xt_symbol_init_well_known();
  for (int i = 0; i < XT_WELL_KNOWN_COUNT; i++) {
    if (strcmp(name, XT_WELL_KNOWN_NAMES[i]) == 0) return g_well_known[i];
  }
  return XT_UNDEFINED;
}

xt_value xt_symbol_get(xt_value name) {
  const char *text = xt_string_data(name);
  if (!text) return XT_UNDEFINED;
  return xt_symbol_well_known(text);
}

/* -- description / toString ---------------------------------------------- */

xt_value xt_symbol_description(xt_value value) {
  xt_symbol_data *symbol = xt_as_symbol(value);
  return symbol ? symbol->description : XT_UNDEFINED;
}

xt_value xt_symbol_to_string(xt_value value) {
  xt_symbol_data *symbol = xt_as_symbol(value);
  if (!symbol) {
    xt_throw(xt_string_from_cstr(
        "TypeError: Symbol.prototype.toString requires that 'this' be a Symbol"));
  }
  xt_string *description =
      XT_IS_STRING(symbol->description) ? xt_as_string(symbol->description) : NULL;
  size_t length = description ? description->length : 0;
  char *buffer = (char *)malloc(8 + length);
  if (!buffer) abort();
  memcpy(buffer, "Symbol(", 7);
  if (description) memcpy(buffer + 7, description->data, length);
  buffer[7 + length] = ')';
  xt_value result = xt_string_new(buffer, 8 + length);
  free(buffer);
  return result;
}

/* -- constructor and statics ---------------------------------------------- */

xt_value xt_symbol(int32_t argc, xt_value *argv) {
  xt_value description = xt_arg_at(argc, argv, 0);
  if (description != XT_UNDEFINED) description = xt_to_string(description);
  return xt_symbol_new(description);
}

/* Global symbol registry backing `Symbol.for` / `Symbol.keyFor`. */
static xt_value g_symbol_registry = XT_UNDEFINED;

xt_value xt_symbol_for(xt_value key) {
  xt_value keyString = xt_to_string(key);
  if (g_symbol_registry == XT_UNDEFINED) g_symbol_registry = xt_object_new();
  xt_value existing = xt_object_get(g_symbol_registry, keyString);
  if (existing != XT_UNDEFINED) return existing;
  xt_value symbol = xt_symbol_new(keyString);
  xt_object_set(g_symbol_registry, keyString, symbol);
  return symbol;
}

xt_value xt_symbol_key_for(xt_value value) {
  if (!xt_is_symbol(value)) {
    xt_throw(xt_string_from_cstr("TypeError: Symbol.keyFor requires a symbol"));
  }
  if (g_symbol_registry == XT_UNDEFINED) return XT_UNDEFINED;
  xt_value keys = xt_object_keys(g_symbol_registry);
  xt_array *array = xt_as_array(keys);
  if (!array) return XT_UNDEFINED;
  for (uint32_t i = 0; i < array->length; i++) {
    xt_value key = array->items[i];
    if (xt_object_get(g_symbol_registry, key) == value) return key;
  }
  return XT_UNDEFINED;
}

xt_value xt_symbol_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return XT_UNDEFINED;
  if (strcmp(fn, "for") == 0) return xt_symbol_for(xt_arg_at(argc, argv, 0));
  if (strcmp(fn, "keyFor") == 0) return xt_symbol_key_for(xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}
