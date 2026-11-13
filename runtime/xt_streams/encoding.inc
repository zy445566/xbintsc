/* Queuing strategies and the TextEncoderStream / TextDecoderStream subset. */

#ifndef XT_STREAMS_ENCODING_INC
#define XT_STREAMS_ENCODING_INC

/* TextEncoder / TextDecoder constructors live in xt_text_codec.c. */
xt_value xt_text_encoder_ctor(int32_t argc, xt_value *argv);
xt_value xt_text_decoder_ctor(int32_t argc, xt_value *argv);

/* -- queuing strategies --------------------------------------------------- */

static xt_value xts_count_size(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_number(1);
}

static xt_value xts_byte_size(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xt_value chunk = xt_arg_at(argc, argv, 0);
  xt_value length = XT_IS_OBJECT(chunk) ? xts_get(chunk, "byteLength") : XT_UNDEFINED;
  if (XT_IS_NUMBER(length)) return length;
  return xt_number(0);
}

static xt_value xts_strategy_new(xt_value proto, xt_value options) {
  xt_value strategy = xt_object_new_with_proto(proto);
  xt_value hwm = XT_IS_OBJECT(options) ? xts_get(options, "highWaterMark") : XT_UNDEFINED;
  if (!XT_IS_NUMBER(hwm)) hwm = xt_number(1);
  xts_set(strategy, "highWaterMark", hwm);
  return strategy;
}

xt_value xt_stream_count_strategy_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xts_set(proto, "size", xts_closure((void *)xts_count_size, 0, NULL));
  return proto;
}

xt_value xt_stream_byte_strategy_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xts_set(proto, "size", xts_closure((void *)xts_byte_size, 0, NULL));
  return proto;
}

xt_value xt_count_queuing_strategy_ctor(int32_t argc, xt_value *argv) {
  return xts_strategy_new(xt_stream_count_strategy_proto(), xt_arg_at(argc, argv, 0));
}

xt_value xt_byte_length_queuing_strategy_ctor(int32_t argc, xt_value *argv) {
  return xts_strategy_new(xt_stream_byte_strategy_proto(), xt_arg_at(argc, argv, 0));
}

/* -- text encoder stream -------------------------------------------------- */

static xt_value xts_text_encoder_transform(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xt_value chunk = xt_arg_at(argc, argv, 0);
  xt_value controller = xt_arg_at(argc, argv, 1);
  xt_value encoder = xt_text_encoder_ctor(0, NULL);
  xt_value text = xt_to_string(chunk);
  xt_value bytes = xt_call_method(encoder, xt_string_from_cstr("encode"), 1, &text);
  xt_call_method(controller, xt_string_from_cstr("enqueue"), 1, &bytes);
  return XT_UNDEFINED;
}

static xt_value xts_text_stream_flush(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

xt_value xt_text_encoder_stream_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value transformer = xt_object_new();
  xts_set(transformer, "transform", xts_closure((void *)xts_text_encoder_transform, 0, NULL));
  xts_set(transformer, "flush", xts_closure((void *)xts_text_stream_flush, 0, NULL));
  xt_value stream = xts_transform_create(transformer, XT_UNDEFINED, XT_UNDEFINED);
  xts_set(stream, "encoding", xt_string_from_cstr("utf-8"));
  xt_value state = xts_get(stream, "__state");
  if (XT_IS_OBJECT(state)) xts_set(state, "textKind", xt_number(1));
  return stream;
}

/* -- text decoder stream -------------------------------------------------- */

static xt_value xts_text_decoder_transform(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  xt_value chunk = xt_arg_at(argc, argv, 0);
  xt_value controller = xt_arg_at(argc, argv, 1);
  xt_value decoder = xt_text_decoder_ctor(0, NULL);
  xt_value text = xt_call_method(decoder, xt_string_from_cstr("decode"), 1, &chunk);
  xt_call_method(controller, xt_string_from_cstr("enqueue"), 1, &text);
  return XT_UNDEFINED;
}

xt_value xt_text_decoder_stream_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_value transformer = xt_object_new();
  xts_set(transformer, "transform", xts_closure((void *)xts_text_decoder_transform, 0, NULL));
  xts_set(transformer, "flush", xts_closure((void *)xts_text_stream_flush, 0, NULL));
  xt_value stream = xts_transform_create(transformer, XT_UNDEFINED, XT_UNDEFINED);
  xts_set(stream, "encoding", xt_string_from_cstr("utf-8"));
  xt_value state = xts_get(stream, "__state");
  if (XT_IS_OBJECT(state)) xts_set(state, "textKind", xt_number(2));
  return stream;
}

#endif /* XT_STREAMS_ENCODING_INC */
