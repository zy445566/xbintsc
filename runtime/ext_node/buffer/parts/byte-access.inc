/* -- byte access ---------------------------------------------------------- */

/*
 * The bytes are *not* indexed properties.
 *
 * A Buffer used to be a plain object whose bytes were stored as the properties
 * "0", "1", ... — one `xt_object_insert_property` per byte. That insert walks
 * the integer prefix of the key list and rebuilds the hidden-class shape, so
 * building a Buffer was O(n^2): `Buffer.alloc(30000)` did not finish in fifteen
 * minutes. Real programs hit it: undici's base64 decoder turns a 58 KB string
 * into a 44 KB Buffer.
 *
 * Instead a Buffer keeps its bytes in an ArrayBuffer under a private symbol and
 * installs dynamic-property hooks that answer `buf[i]` / `buf[i] = 65` from it.
 * Construction is then one bulk copy, a byte read is an array index, and the
 * hooks only run when ordinary lookup misses (a real property or a prototype
 * method still wins). The ArrayBuffer representation stores one number per
 * byte, which costs memory but is garbage collected without a finalizer; a raw
 * byte store would be a drop-in replacement behind `xt_buffer_byte` /
 * `xt_buffer_put`.
 */

static xt_value xt_buffer_store_key_value = XT_UNDEFINED;

static xt_value xt_buffer_store_key(void) {
  if (!xt_is_symbol(xt_buffer_store_key_value)) {
    xt_buffer_store_key_value = xt_symbol_new(xt_string_from_cstr("__bufferBytes"));
    xt_gc_add_root(&xt_buffer_store_key_value);
  }
  return xt_buffer_store_key_value;
}

/* A property key that is a canonical array index: "0", "17", … (but not "01",
 * "1.5" or "-1", which stay ordinary properties). */
static int xt_buffer_index_key(xt_value key, uint32_t *out) {
  if (!XT_IS_STRING(key)) return 0;
  xt_string *string = xt_as_string(key);
  if (string->length == 0 || string->length > 10) return 0;
  const char *bytes = xt_string_bytes(string);
  if (bytes[0] == '0' && string->length != 1) return 0;
  uint64_t value = 0;
  for (uint32_t i = 0; i < string->length; i++) {
    char ch = bytes[i];
    if (ch < '0' || ch > '9') return 0;
    value = value * 10 + (uint64_t)(ch - '0');
  }
  if (value > 4294967294ULL) return 0;
  *out = (uint32_t)value;
  return 1;
}

static xt_value xt_buffer_store(xt_value buffer) {
  if (!XT_IS_OBJECT(buffer)) return XT_UNDEFINED;
  xt_value store = xt_object_get(buffer, xt_buffer_store_key());
  return xt_is_array_buffer(store) ? store : XT_UNDEFINED;
}

static uint32_t xt_buffer_length(xt_value buffer) {
  double length = xt_to_number(xt_object_get_cstr(buffer, "length"));
  if (!(length >= 0)) return 0;
  uint32_t declared = (uint32_t)length;
  xt_value store = xt_buffer_store(buffer);
  if (XT_IS_UNDEFINED(store)) return declared;
  uint32_t stored = xt_array_buffer_byte_length(store);
  return declared < stored ? declared : stored;
}

static unsigned char xt_buffer_byte(xt_value buffer, uint32_t index) {
  xt_value store = xt_buffer_store(buffer);
  if (XT_IS_UNDEFINED(store) || index >= xt_array_buffer_byte_length(store)) return 0;
  return xt_array_buffer_get_byte(store, index);
}

static void xt_buffer_put(xt_value buffer, uint32_t index, unsigned char byte) {
  xt_value store = xt_buffer_store(buffer);
  if (XT_IS_UNDEFINED(store) || index >= xt_array_buffer_byte_length(store)) return;
  xt_array_buffer_set_byte(store, index, byte);
}

/* -- dynamic index access ------------------------------------------------- */

static xt_value xt_buffer_hook_get(xt_value self, xt_value key, void *data) {
  (void)data;
  uint32_t index = 0;
  if (!xt_buffer_index_key(key, &index)) return XT_UNDEFINED;
  xt_value store = xt_buffer_store(self);
  if (XT_IS_UNDEFINED(store) || index >= xt_array_buffer_byte_length(store)) return XT_UNDEFINED;
  return xt_number((double)xt_array_buffer_get_byte(store, index));
}

static int xt_buffer_hook_set(xt_value self, xt_value key, xt_value value, void *data) {
  (void)data;
  uint32_t index = 0;
  if (!xt_buffer_index_key(key, &index)) return 0;
  /* Out of range writes are dropped instead of creating a property, like a
   * real Buffer: `buf[buf.length] = 1` is a no-op and cannot grow it. */
  xt_buffer_put(self, index, (unsigned char)((int32_t)xt_to_number(value) & 0xff));
  return 1;
}

static int xt_buffer_hook_has(xt_value self, xt_value key, void *data) {
  (void)data;
  uint32_t index = 0;
  return xt_buffer_index_key(key, &index) && index < xt_buffer_length(self);
}

static xt_value xt_buffer_hook_keys(xt_value self, void *data) {
  (void)data;
  uint32_t length = xt_buffer_length(self);
  xt_value keys = xt_array_new(0, NULL);
  char text[16];
  for (uint32_t i = 0; i < length; i++) {
    snprintf(text, sizeof(text), "%u", i);
    xt_array_push(keys, xt_string_from_cstr(text));
  }
  return keys;
}

static const xt_object_hooks xt_buffer_hooks = {
    xt_buffer_hook_get, xt_buffer_hook_set, xt_buffer_hook_has, NULL, xt_buffer_hook_keys,
};

/* -- construction --------------------------------------------------------- */

static xt_value xt_buffer_new(uint32_t length, const unsigned char *data) {
  xt_value buffer = xt_object_new_with_proto(xt_buffer_proto());
  xt_value size = xt_number((double)length);
  xt_value store = xt_array_buffer_ctor(1, &size);
  if (length > 0 && data != NULL) xt_array_buffer_copy_bytes_in(store, 0, data, length);
  xt_object_set(buffer, xt_buffer_store_key(), store);
  xt_node_set(buffer, "length", xt_number((double)length));
  xt_object_set_hooks(buffer, &xt_buffer_hooks, NULL);
  return buffer;
}

static int xt_buffer_is(xt_value value) {
  if (!XT_IS_OBJECT(value)) return 0;
  return xt_truthy(xt_seq(xt_object_get_prototype(value), xt_buffer_proto()));
}
