/* Global constructors, statics and `instanceof` support for the Web Streams. */

#ifndef XT_STREAMS_CONSTRUCTORS_INC
#define XT_STREAMS_CONSTRUCTORS_INC

xt_value xt_readable_stream_ctor(int32_t argc, xt_value *argv) {
  xt_value state = xts_state_new(XT_STREAMS_READABLE);
  xt_value source = xt_arg_at(argc, argv, 0);
  xt_value strategy = xt_arg_at(argc, argv, 1);
  return xts_readable_create(state, source, strategy);
}

xt_value xt_writable_stream_ctor(int32_t argc, xt_value *argv) {
  xt_value state = xts_state_new(XT_STREAMS_WRITABLE);
  xt_value sink = xt_arg_at(argc, argv, 0);
  xt_value strategy = xt_arg_at(argc, argv, 1);
  return xts_writable_create(state, sink, strategy);
}

xt_value xt_transform_stream_ctor(int32_t argc, xt_value *argv) {
  xt_value transformer = xt_arg_at(argc, argv, 0);
  xt_value readable_strategy = xt_arg_at(argc, argv, 1);
  xt_value writable_strategy = xt_arg_at(argc, argv, 2);
  return xts_transform_create(transformer, readable_strategy, writable_strategy);
}

/* First-class constructor values for imported bindings. `xt_builtin_constructor`
 * installs the same prototype the direct C constructor uses, so an imported
 * `ReadableStream` supports `instanceof` and statics just like the global. */
static xt_value xts_constructor_value(const char *name) {
  return xt_builtin_constructor(xt_string_from_cstr(name));
}

xt_value xt_readable_stream_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("ReadableStream");
}

xt_value xt_writable_stream_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("WritableStream");
}

xt_value xt_transform_stream_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("TransformStream");
}

xt_value xt_count_strategy_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("CountQueuingStrategy");
}

xt_value xt_byte_strategy_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("ByteLengthQueuingStrategy");
}

xt_value xt_text_encoder_stream_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("TextEncoderStream");
}

xt_value xt_text_decoder_stream_constructor_value(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_constructor_value("TextDecoderStream");
}

int xt_streams_has_static(const char *name, const char *method) {
  if (!name || !method) return 0;
  if (strcmp(name, "ReadableStream") == 0 && strcmp(method, "from") == 0) return 1;
  return 0;
}

static xt_value xts_from_async_step(xt_value pipe);

static xt_value xts_from_async_on_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xt_value pipe = xt_closure_env(env, 0);
  xt_value state = xts_get(pipe, "state");
  if (xts_int(state, "state") != XT_STREAMS_RS_READABLE) return XT_UNDEFINED;
  xt_value result = xt_arg_at(argc, argv, 0);
  if (xt_truthy(xts_get(result, "done"))) {
    if (!xts_truthy_field(state, "closeRequested")) xts_readable_controller_close(state);
    return XT_UNDEFINED;
  }
  xts_readable_controller_enqueue(state, xts_get(result, "value"));
  xts_from_async_step(pipe);
  return XT_UNDEFINED;
}

static xt_value xts_from_async_on_error(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value pipe = xt_closure_env(env, 0);
  xts_readable_error(xts_get(pipe, "state"), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_from_async_step(xt_value pipe) {
  xt_value promise = xt_async_iter_next(xts_get(pipe, "iterator"));
  xt_value environment[1] = {pipe};
  xts_then(promise, xts_closure((void *)xts_from_async_on_read, 1, environment),
           xts_closure((void *)xts_from_async_on_error, 1, environment));
  return XT_UNDEFINED;
}

static int xts_enqueue_iterable(xt_value state, xt_value source) {
  if (source == XT_UNDEFINED || source == XT_NULL) return 0;
  xt_value iterator = xt_iter_open(source);
  if (iterator == XT_UNDEFINED || iterator == XT_NULL) return -1;
  int index = 0;
  while (1) {
    xt_value index_value = xt_number((double)index);
    if (!xt_truthy(xt_iter_has(iterator, index_value))) break;
    xt_value value = xt_iter_value(iterator, index_value);
    xts_readable_controller_enqueue(state, value);
    index++;
    if (index > 100000000) break;
  }
  return 0;
}

xt_value xt_streams_static(xt_value name, int32_t argc, xt_value *argv) {
  (void)name;
  xt_value source = xt_arg_at(argc, argv, 0);
  xt_value state = xts_state_new(XT_STREAMS_READABLE);
  xt_value stream = xts_readable_create(state, XT_UNDEFINED, XT_UNDEFINED);

  /* Prefer the asynchronous iteration protocol when the source exposes one;
   * its first chunk is produced when the returned stream is read. */
  if (XT_IS_OBJECT(source)) {
    xt_value async_method = xt_get(source, xt_symbol_well_known("asyncIterator"));
    if (XT_IS_FUNCTION(async_method)) {
      xt_value iterator = xt_async_iter_open(source);
      xt_value pipe = xt_object_new();
      xts_set(pipe, "state", state);
      xts_set(pipe, "iterator", iterator);
      xts_from_async_step(pipe);
      return stream;
    }
  }

  if (xts_enqueue_iterable(state, source) < 0) {
    xts_readable_error(state, xts_type_error("TypeError: ReadableStream.from expects an iterable"));
    return stream;
  }
  xts_readable_controller_close(state);
  return stream;
}

int xt_streams_instance_of(xt_value value, int32_t kind) {
  if (!XT_IS_OBJECT(value)) return 0;
  xt_value state = xts_get(value, "__state");
  if (!XT_IS_OBJECT(state)) return 0;
  int stream_kind = xts_int(state, "kind");
  switch (kind) {
    case 0:
      return stream_kind == XT_STREAMS_READABLE;
    case 1:
      return stream_kind == XT_STREAMS_WRITABLE;
    case 2:
      return stream_kind == XT_STREAMS_TRANSFORM;
    case 3:
      return stream_kind == XT_STREAMS_TRANSFORM && xts_int(state, "textKind") == 1;
    case 4:
      return stream_kind == XT_STREAMS_TRANSFORM && xts_int(state, "textKind") == 2;
    default:
      return 0;
  }
}

/* -- illegal constructors ------------------------------------------------- */

static xt_value xts_illegal_constructor(const char *name) {
  char message[160];
  snprintf(message, sizeof(message), "TypeError: Illegal constructor: %s", name);
  xt_value error = xts_type_error(message);
  xt_throw(error);
  return XT_UNDEFINED;
}

xt_value xt_readable_stream_default_reader_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("ReadableStreamDefaultReader");
}

xt_value xt_readable_stream_byob_reader_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("ReadableStreamBYOBReader");
}

xt_value xt_readable_stream_byob_request_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("ReadableStreamBYOBRequest");
}

xt_value xt_readable_stream_default_controller_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("ReadableStreamDefaultController");
}

xt_value xt_writable_stream_default_writer_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("WritableStreamDefaultWriter");
}

xt_value xt_writable_stream_default_controller_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("WritableStreamDefaultController");
}

xt_value xt_transform_stream_default_controller_ctor(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xts_illegal_constructor("TransformStreamDefaultController");
}

#endif /* XT_STREAMS_CONSTRUCTORS_INC */
