/* -- prototype ------------------------------------------------------------ */

static xt_value xt_buffer_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_define_method(proto, "toString", (void *)xt_buffer_method_to_string);
  xt_node_define_method(proto, "toJSON", (void *)xt_buffer_method_to_json);
  xt_node_define_method(proto, "slice", (void *)xt_buffer_method_slice);
  xt_node_define_method(proto, "subarray", (void *)xt_buffer_method_slice);
  xt_node_define_method(proto, "equals", (void *)xt_buffer_method_equals);
  xt_node_define_method(proto, "compare", (void *)xt_buffer_method_compare);
  xt_node_define_method(proto, "copy", (void *)xt_buffer_method_copy);
  xt_node_define_method(proto, "write", (void *)xt_buffer_method_write);
  xt_node_define_method(proto, "fill", (void *)xt_buffer_method_fill);
  xt_node_define_method(proto, "reverse", (void *)xt_buffer_method_reverse);
  xt_node_define_method(proto, "indexOf", (void *)xt_buffer_method_index_of_fwd);
  xt_node_define_method(proto, "lastIndexOf", (void *)xt_buffer_method_index_of_rev);
  xt_node_define_method(proto, "includes", (void *)xt_buffer_method_includes);
  xt_node_define_method(proto, "keys", (void *)xt_buffer_method_keys);
  xt_node_define_method(proto, "values", (void *)xt_buffer_method_values);
  xt_node_define_method(proto, "readUInt8", (void *)xt_buffer_read_u8);
  xt_node_define_method(proto, "readInt8", (void *)xt_buffer_read_u8s);
  xt_node_define_method(proto, "writeUInt8", (void *)xt_buffer_write_u8);
  xt_node_define_method(proto, "writeInt8", (void *)xt_buffer_write_u8);
  xt_node_define_method(proto, "readUInt16LE", (void *)xt_buffer_read_u16);
  xt_node_define_method(proto, "readUInt16BE", (void *)xt_buffer_read_u16be);
  xt_node_define_method(proto, "readInt16LE", (void *)xt_buffer_read_u16s);
  xt_node_define_method(proto, "readInt16BE", (void *)xt_buffer_read_u16bes);
  xt_node_define_method(proto, "writeUInt16LE", (void *)xt_buffer_write_u16);
  xt_node_define_method(proto, "writeUInt16BE", (void *)xt_buffer_write_u16be);
  xt_node_define_method(proto, "readUInt32LE", (void *)xt_buffer_read_u32);
  xt_node_define_method(proto, "readUInt32BE", (void *)xt_buffer_read_u32be);
  xt_node_define_method(proto, "readInt32LE", (void *)xt_buffer_read_u32s);
  xt_node_define_method(proto, "readInt32BE", (void *)xt_buffer_read_u32bes);
  xt_node_define_method(proto, "writeUInt32LE", (void *)xt_buffer_write_u32);
  xt_node_define_method(proto, "writeUInt32BE", (void *)xt_buffer_write_u32be);
  (void)xt_buffer_read_float;
  return proto;
}

/* Enforce prototype creation on first use so `Buffer.from` marks the object. */
void xt_buffer_ensure_proto(void) { (void)xt_buffer_proto(); }

/* Cross-module helpers used by the net/http/dgram modules. */
int xt_node_is_buffer(xt_value value) { return xt_buffer_is(value); }

unsigned char *xt_node_buffer_bytes(xt_value value, size_t *outLength) {
  uint32_t length = xt_buffer_length(value);
  unsigned char *bytes = (unsigned char *)malloc(length ? length : 1);
  for (uint32_t i = 0; i < length; i++) bytes[i] = xt_buffer_byte(value, i);
  *outLength = length;
  return bytes;
}
