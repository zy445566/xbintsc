/*
 * Node.js `stream` for xbintsc.
 *
 * Because xbintsc has no persistent event loop, streams are synchronous and
 * event-based: `on('data')` flushes anything buffered by `push`, and `write`
 * delivers immediately. The API surface (`Readable`, `Writable`, `Duplex`,
 * `Transform`, `PassThrough`, `pipe`, `write`, `end`, `push`, `read`) mirrors
 * Node closely enough for the common callback styles.
 */

#include "../node_common.h"

static xt_value xt_stream_proto(void);

typedef enum {
  XT_STREAM_READABLE,
  XT_STREAM_WRITABLE,
  XT_STREAM_DUPLEX,
  XT_STREAM_TRANSFORM,
  XT_STREAM_PASSTHROUGH,
} xt_stream_kind;

static xt_value xt_stream_new(xt_stream_kind kind) {
  xt_value stream = xt_object_new_with_proto(xt_stream_proto());
  xt_node_set(stream, "__kind", xt_number((double)kind));
  xt_node_set(stream, "__readable", xt_array_new(0, NULL));
  return stream;
}

static int xt_stream_kind_of(xt_value stream) {
  return (int)xt_to_number(xt_object_get_cstr(stream, "__kind"));
}

static xt_value xt_stream_buffer(xt_value stream) {
  xt_value buffer = xt_object_get_cstr(stream, "__readable");
  if (!XT_IS_ARRAY(buffer)) {
    buffer = xt_array_new(0, NULL);
    xt_node_set(stream, "__readable", buffer);
  }
  return buffer;
}

static int xt_stream_has_data_listener(xt_value stream) {
  xt_value listeners = xt_node_listeners(stream, "data", 0);
  return XT_IS_ARRAY(listeners) && xt_to_number(xt_array_length(listeners)) > 0;
}

/* -- readable surface ----------------------------------------------------- */

static xt_value xt_stream_deliver(xt_value stream, xt_value chunk) {
  if (xt_stream_has_data_listener(stream)) {
    xt_node_emit1(stream, "data", chunk);
  } else {
    xt_array_push(xt_stream_buffer(stream), chunk);
  }
  return xt_bool(1);
}

static xt_value xt_stream_method_push(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value chunk = xt_arg(argc, argv, 0);
  if (xt_truthy(xt_is_nullish(chunk))) {
    xt_node_set(thisValue, "__ended", xt_bool(1));
    xt_node_emit0(thisValue, "end");
    return xt_bool(0);
  }
  return xt_stream_deliver(thisValue, chunk);
}

static xt_value xt_stream_method_unshift(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value chunk = xt_arg(argc, argv, 0);
  if (xt_truthy(xt_is_nullish(chunk))) return xt_bool(0);
  xt_value buffer = xt_stream_buffer(thisValue);
  xt_value rebuilt = xt_array_new(1, &chunk);
  int32_t count = (int32_t)xt_to_number(xt_array_length(buffer));
  for (int32_t i = 0; i < count; i++) xt_array_push(rebuilt, xt_array_get(buffer, xt_number((double)i)));
  xt_node_set(thisValue, "__readable", rebuilt);
  return xt_bool(1);
}

static xt_value xt_stream_method_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value buffer = xt_stream_buffer(thisValue);
  int32_t count = (int32_t)xt_to_number(xt_array_length(buffer));
  if (count == 0) return xt_null();
  xt_value first = xt_array_get(buffer, xt_number(0));
  xt_value rest = xt_array_new(0, NULL);
  for (int32_t i = 1; i < count; i++) xt_array_push(rest, xt_array_get(buffer, xt_number((double)i)));
  xt_node_set(thisValue, "__readable", rest);
  return first;
}

static xt_value xt_stream_method_set_encoding(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_node_set(thisValue, "__encoding", xt_arg(argc, argv, 0));
  return thisValue;
}

static xt_value xt_stream_method_pause(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_node_set(thisValue, "__paused", xt_bool(1));
  return thisValue;
}

static xt_value xt_stream_method_resume(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_node_set(thisValue, "__paused", xt_bool(0));
  xt_node_emit0(thisValue, "resume");
  return thisValue;
}

static xt_value xt_stream_method_is_paused(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xt_bool(xt_truthy(xt_object_get_cstr(thisValue, "__paused")));
}

/* -- writable surface ----------------------------------------------------- */

static xt_value xt_stream_transform_flush(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv);

static xt_value xt_stream_method_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value chunk = xt_arg(argc, argv, 0);
  xt_value encoding = xt_arg(argc, argv, 1);
  xt_value callback = xt_arg(argc, argv, 2);
  int kind = xt_stream_kind_of(thisValue);

  if (kind == XT_STREAM_TRANSFORM) {
    xt_value transform = xt_object_get_cstr(thisValue, "__transform");
    if (XT_IS_FUNCTION(transform)) {
      xt_value cb = xt_closure_new((void *)xt_stream_transform_flush, 1, &thisValue);
      xt_value args[3];
      args[0] = chunk;
      args[1] = encoding;
      args[2] = cb;
      xt_call_with_this(transform, thisValue, 3, args);
    }
  } else if (kind == XT_STREAM_PASSTHROUGH || kind == XT_STREAM_DUPLEX) {
    xt_stream_deliver(thisValue, chunk);
  }

  xt_value writeFn = xt_object_get_cstr(thisValue, "__write");
  if (XT_IS_FUNCTION(writeFn)) {
    xt_value args[3];
    args[0] = chunk;
    args[1] = encoding;
    args[2] = callback;
    xt_call_with_this(writeFn, thisValue, 3, args);
  }

  if (XT_IS_FUNCTION(callback)) {
    xt_value cbArgs[1];
    cbArgs[0] = xt_null();
    xt_call_with_this(callback, xt_undefined(), 1, cbArgs);
  }
  xt_node_emit0(thisValue, "drain");
  return xt_bool(1);
}

static xt_value xt_stream_method_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value chunk = xt_arg(argc, argv, 0);
  xt_value callback = XT_UNDEFINED;
  if (argc >= 1 && !xt_truthy(xt_is_nullish(chunk))) {
    xt_value writeFn = xt_object_get_cstr(thisValue, "write");
    if (XT_IS_FUNCTION(writeFn)) xt_call_with_this(writeFn, thisValue, 1, &chunk);
  }
  for (int32_t i = 0; i < argc; i++) {
    if (XT_IS_FUNCTION(argv[i])) callback = argv[i];
  }
  xt_node_set(thisValue, "__writeEnded", xt_bool(1));
  xt_node_emit0(thisValue, "finish");
  int kind = xt_stream_kind_of(thisValue);
  if (kind != XT_STREAM_WRITABLE) {
    xt_node_set(thisValue, "__ended", xt_bool(1));
    xt_node_emit0(thisValue, "end");
  }
  if (XT_IS_FUNCTION(callback)) xt_call_with_this(callback, xt_undefined(), 0, NULL);
  return thisValue;
}

static xt_value xt_stream_method_destroy(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value error = xt_arg(argc, argv, 0);
  xt_node_set(thisValue, "__destroyed", xt_bool(1));
  if (!xt_truthy(xt_is_nullish(error))) xt_node_emit1(thisValue, "error", error);
  xt_node_emit0(thisValue, "close");
  return thisValue;
}

static xt_value xt_stream_method_cork(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return thisValue;
}

static xt_value xt_stream_method_uncork(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_node_emit0(thisValue, "drain");
  return thisValue;
}

/* -- pipe ----------------------------------------------------------------- */

static xt_value xt_stream_pipe_data(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value destination = xt_closure_env(env, 0);
  xt_value chunk = xt_arg(argc, argv, 0);
  if (!xt_truthy(xt_is_nullish(chunk))) {
    xt_value writeFn = xt_object_get_cstr(destination, "write");
    if (XT_IS_FUNCTION(writeFn)) xt_call_with_this(writeFn, destination, 1, &chunk);
  }
  return xt_undefined();
}

static xt_value xt_stream_pipe_end(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value destination = xt_closure_env(env, 0);
  xt_value endFn = xt_object_get_cstr(destination, "end");
  if (XT_IS_FUNCTION(endFn)) xt_call_with_this(endFn, destination, 0, NULL);
  return xt_undefined();
}

static xt_value xt_stream_method_pipe(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value destination = xt_arg(argc, argv, 0);
  if (xt_truthy(xt_is_nullish(destination))) return destination;

  xt_value onData = xt_closure_new((void *)xt_stream_pipe_data, 1, &destination);
  xt_value onEnd = xt_closure_new((void *)xt_stream_pipe_end, 1, &destination);
  xt_array_push(xt_node_listeners(thisValue, "data", 1), onData);
  xt_array_push(xt_node_listeners(thisValue, "end", 1), onEnd);

  xt_value buffer = xt_stream_buffer(thisValue);
  int32_t count = (int32_t)xt_to_number(xt_array_length(buffer));
  for (int32_t i = 0; i < count; i++) {
    xt_value chunk = xt_array_get(buffer, xt_number((double)i));
    xt_value writeFn = xt_object_get_cstr(destination, "write");
    if (XT_IS_FUNCTION(writeFn)) xt_call_with_this(writeFn, destination, 1, &chunk);
  }
  xt_node_set(thisValue, "__readable", xt_array_new(0, NULL));
  if (xt_truthy(xt_object_get_cstr(thisValue, "__ended"))) {
    xt_value endFn = xt_object_get_cstr(destination, "end");
    if (XT_IS_FUNCTION(endFn)) xt_call_with_this(endFn, destination, 0, NULL);
  }
  return destination;
}

/* -- transform ------------------------------------------------------------ */

static xt_value xt_stream_transform_flush(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value stream = xt_closure_env(env, 0);
  xt_value error = xt_arg(argc, argv, 0);
  if (!xt_truthy(xt_is_nullish(error))) {
    xt_node_emit1(stream, "error", error);
    return xt_undefined();
  }
  for (int32_t i = 1; i < argc; i++) {
    xt_value data = argv[i];
    if (!xt_truthy(xt_is_nullish(data))) xt_stream_deliver(stream, data);
  }
  return xt_undefined();
}

/* -- construction --------------------------------------------------------- */

static xt_value xt_stream_from_options(int32_t argc, xt_value *argv, xt_stream_kind kind) {
  xt_value stream = xt_stream_new(kind);
  xt_value options = argc > 0 ? argv[0] : XT_UNDEFINED;
  if (XT_IS_OBJECT(options)) {
    xt_value transform = xt_object_get_cstr(options, "transform");
    if (XT_IS_FUNCTION(transform)) xt_node_set(stream, "__transform", transform);
    xt_value write = xt_object_get_cstr(options, "write");
    if (XT_IS_FUNCTION(write)) xt_node_set(stream, "__write", write);
    xt_value read = xt_object_get_cstr(options, "read");
    if (XT_IS_FUNCTION(read)) xt_node_set(stream, "__read", read);
    xt_value objectMode = xt_object_get_cstr(options, "objectMode");
    if (!xt_truthy(xt_is_nullish(objectMode))) xt_node_set(stream, "__objectMode", objectMode);
    /* A leading function argument is shorthand for `{ transform }`/`{ write }`. */
  } else if (XT_IS_FUNCTION(options)) {
    if (kind == XT_STREAM_TRANSFORM || kind == XT_STREAM_DUPLEX) xt_node_set(stream, "__transform", options);
    else xt_node_set(stream, "__write", options);
  } else if (argc > 1 && XT_IS_FUNCTION(argv[1])) {
    if (kind == XT_STREAM_TRANSFORM) xt_node_set(stream, "__transform", argv[0]);
    xt_node_set(stream, "__write", argv[1]);
  }
  return stream;
}

xt_value xt_readable_ctor(int32_t argc, xt_value *argv) { return xt_stream_from_options(argc, argv, XT_STREAM_READABLE); }
xt_value xt_writable_ctor(int32_t argc, xt_value *argv) { return xt_stream_from_options(argc, argv, XT_STREAM_WRITABLE); }
xt_value xt_duplex_ctor(int32_t argc, xt_value *argv) { return xt_stream_from_options(argc, argv, XT_STREAM_DUPLEX); }
xt_value xt_transform_ctor(int32_t argc, xt_value *argv) { return xt_stream_from_options(argc, argv, XT_STREAM_TRANSFORM); }
xt_value xt_pass_through_ctor(int32_t argc, xt_value *argv) { return xt_stream_from_options(argc, argv, XT_STREAM_PASSTHROUGH); }

/* `Readable.from(iterable)` and friends: the namespace dispatcher. */
xt_value xt_stream_static(xt_value name, int32_t argc, xt_value *argv) {
  const char *fn = xt_string_data(name);
  if (!fn) return xt_undefined();
  if (strcmp(fn, "from") == 0) {
    xt_value stream = xt_stream_new(XT_STREAM_READABLE);
    xt_value source = xt_arg(argc, argv, 0);
    if (XT_IS_ARRAY(source)) {
      int32_t count = (int32_t)xt_to_number(xt_array_length(source));
      for (int32_t i = 0; i < count; i++) xt_stream_deliver(stream, xt_array_get(source, xt_number((double)i)));
    }
    xt_node_set(stream, "__ended", xt_bool(1));
    xt_node_emit0(stream, "end");
    return stream;
  }
  return xt_undefined();
}

static xt_value xt_stream_proto(void) {
  static xt_value proto = 0;
  if (proto) return proto;
  proto = xt_object_new();
  xt_node_install_emitter(proto);
  xt_node_define_method(proto, "push", (void *)xt_stream_method_push);
  xt_node_define_method(proto, "unshift", (void *)xt_stream_method_unshift);
  xt_node_define_method(proto, "read", (void *)xt_stream_method_read);
  xt_node_define_method(proto, "setEncoding", (void *)xt_stream_method_set_encoding);
  xt_node_define_method(proto, "pause", (void *)xt_stream_method_pause);
  xt_node_define_method(proto, "resume", (void *)xt_stream_method_resume);
  xt_node_define_method(proto, "isPaused", (void *)xt_stream_method_is_paused);
  xt_node_define_method(proto, "write", (void *)xt_stream_method_write);
  xt_node_define_method(proto, "end", (void *)xt_stream_method_end);
  xt_node_define_method(proto, "destroy", (void *)xt_stream_method_destroy);
  xt_node_define_method(proto, "cork", (void *)xt_stream_method_cork);
  xt_node_define_method(proto, "uncork", (void *)xt_stream_method_uncork);
  xt_node_define_method(proto, "pipe", (void *)xt_stream_method_pipe);
  return proto;
}
