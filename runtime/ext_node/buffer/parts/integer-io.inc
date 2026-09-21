/* -- integer readers / writers -------------------------------------------- */

static uint64_t xt_buffer_read_le(xt_value buffer, uint32_t offset, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; i++) value |= ((uint64_t)xt_buffer_byte(buffer, offset + (uint32_t)i)) << (8 * i);
  return value;
}

static uint64_t xt_buffer_read_be(xt_value buffer, uint32_t offset, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; i++) value = (value << 8) | xt_buffer_byte(buffer, offset + (uint32_t)i);
  return value;
}

static void xt_buffer_write_le(xt_value buffer, uint32_t offset, int bytes, uint64_t value) {
  for (int i = 0; i < bytes; i++) xt_buffer_put(buffer, offset + (uint32_t)i, (unsigned char)((value >> (8 * i)) & 0xff));
}

static void xt_buffer_write_be(xt_value buffer, uint32_t offset, int bytes, uint64_t value) {
  for (int i = 0; i < bytes; i++) xt_buffer_put(buffer, offset + (uint32_t)i, (unsigned char)((value >> (8 * (bytes - 1 - i))) & 0xff));
}

static xt_value xt_buffer_read_method(xt_value thisValue, int32_t argc, xt_value *argv, int bytes, int little, int signedValue) {
  uint32_t offset = argc > 0 ? (uint32_t)xt_to_number(argv[0]) : 0;
  uint64_t raw = little ? xt_buffer_read_le(thisValue, offset, bytes) : xt_buffer_read_be(thisValue, offset, bytes);
  double result;
  if (signedValue) {
    int64_t signedRaw = (int64_t)raw;
    int bits = bytes * 8;
    if (bits < 64 && (raw & (1ULL << (bits - 1)))) signedRaw = (int64_t)(raw | (~0ULL << bits));
    result = (double)signedRaw;
  } else {
    result = (double)raw;
  }
  return xt_number(result);
}

static xt_value xt_buffer_write_method(xt_value thisValue, int32_t argc, xt_value *argv, int bytes, int little) {
  uint32_t offset = argc > 1 ? (uint32_t)xt_to_number(argv[1]) : 0;
  uint64_t value = (uint64_t)(int64_t)xt_to_number(xt_arg_at(argc, argv, 0));
  if (little) xt_buffer_write_le(thisValue, offset, bytes, value);
  else xt_buffer_write_be(thisValue, offset, bytes, value);
  return xt_number((double)(offset + (uint32_t)bytes));
}

#define XT_BUFFER_DEFINE_IO(name, bytes, little, signedValue)                                                    \
  static xt_value xt_buffer_read_##name(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {        \
    (void)env;                                                                                                   \
    return xt_buffer_read_method(thisValue, argc, argv, bytes, little, signedValue);                            \
  }                                                                                                              \
  static xt_value xt_buffer_write_##name(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {       \
    (void)env;                                                                                                   \
    return xt_buffer_write_method(thisValue, argc, argv, bytes, little);                                        \
  }

#define XT_BUFFER_DEFINE_SIGNED_IO(name, bytes, little)                                                          \
  XT_BUFFER_DEFINE_IO(name, bytes, little, 0)                                                                    \
  static xt_value xt_buffer_read_##name##s(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {     \
    (void)env;                                                                                                   \
    return xt_buffer_read_method(thisValue, argc, argv, bytes, little, 1);                                      \
  }

XT_BUFFER_DEFINE_SIGNED_IO(u8, 1, 1)
XT_BUFFER_DEFINE_SIGNED_IO(u16, 2, 1)
static xt_value xt_buffer_read_u16be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 2, 0, 0);
}
static xt_value xt_buffer_write_u16be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_write_method(thisValue, argc, argv, 2, 0);
}
static xt_value xt_buffer_read_u16bes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 2, 0, 1);
}
XT_BUFFER_DEFINE_SIGNED_IO(u32, 4, 1)
static xt_value xt_buffer_read_u32be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 4, 0, 0);
}
static xt_value xt_buffer_write_u32be(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_write_method(thisValue, argc, argv, 4, 0);
}
static xt_value xt_buffer_read_u32bes(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xt_buffer_read_method(thisValue, argc, argv, 4, 0, 1);
}

static xt_value xt_buffer_read_float(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv, int little, int doublePrecision) {
  (void)env;
  (void)thisValue;
  (void)argc;
  (void)argv;
  (void)little;
  (void)doublePrecision;
  return xt_number(0);
}

static xt_value xt_buffer_method_keys(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(result, xt_number((double)i));
  return result;
}

static xt_value xt_buffer_method_values(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value result = xt_array_new(0, NULL);
  uint32_t length = xt_buffer_length(thisValue);
  for (uint32_t i = 0; i < length; i++) xt_array_push(result, xt_number((double)xt_buffer_byte(thisValue, i)));
  return result;
}
