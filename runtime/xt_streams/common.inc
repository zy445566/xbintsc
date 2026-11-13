/*
 * WHATWG Streams shared helpers.
 *
 * A stream is a plain runtime object with a hidden `__state` property pointing
 * at a mutable state object. Every reader, writer, controller and the stream
 * itself share that object, so the collector reaches the whole graph from the
 * handle the program kept. Keeping state in objects (instead of a C struct)
 * means the GC needs no special tracing hook for streams.
 */

#ifndef XT_STREAMS_COMMON_INC
#define XT_STREAMS_COMMON_INC

/* Stream kinds, stored in `state.kind`. */
#define XT_STREAMS_READABLE 0
#define XT_STREAMS_WRITABLE 1
#define XT_STREAMS_TRANSFORM 2

/* Readable state, stored in `state.state`. */
#define XT_STREAMS_RS_READABLE 0
#define XT_STREAMS_RS_CLOSED 1
#define XT_STREAMS_RS_ERRORED 2

/* Writable state, stored in `state.state`. */
#define XT_STREAMS_WS_WRITABLE 0
#define XT_STREAMS_WS_CLOSING 1
#define XT_STREAMS_WS_CLOSED 2
#define XT_STREAMS_WS_ERRORED 3

/* -- state object helpers ------------------------------------------------- */

static xt_value xts_get(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

static void xts_set(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_string_from_cstr(key), value);
}

static int xts_int(xt_value object, const char *key) {
  return (int)xt_to_number(xts_get(object, key));
}

static int xts_truthy_field(xt_value object, const char *key) {
  return xt_truthy(xts_get(object, key));
}

static xt_value xts_array(void) { return xt_array_new(0, NULL); }

static int xts_array_count(xt_value array) {
  return XT_IS_ARRAY(array) ? (int)xt_to_number(xt_array_length(array)) : 0;
}

static xt_value xts_array_shift(xt_value array) {
  if (!XT_IS_ARRAY(array) || xts_array_count(array) == 0) return XT_UNDEFINED;
  return xt_call_method(array, xt_string_from_cstr("shift"), 0, NULL);
}

static xt_value xts_state_new(int kind) {
  xt_value state = xt_object_new();
  xts_set(state, "kind", xt_number((double)kind));
  xts_set(state, "state", xt_number(0));
  xts_set(state, "storedError", XT_UNDEFINED);
  xts_set(state, "locked", XT_FALSE);
  xts_set(state, "reader", XT_UNDEFINED);
  xts_set(state, "writer", XT_UNDEFINED);
  return state;
}

/* -- promises ------------------------------------------------------------- */

/** Attach handlers to `promise` and suppress the derivative's rejection: the
 * stream machinery always consumes it. Non-promise values resolve normally. */
static xt_value xts_then(xt_value promise, xt_value on_fulfilled, xt_value on_rejected) {
  if (!xt_is_promise(promise)) return xt_promise_resolve(promise);
  int handled = 0;
  xt_value args[2];
  args[0] = on_fulfilled;
  args[1] = on_rejected;
  xt_value result = xt_promise_method(promise, "then", 2, args, &handled);
  if (xt_is_promise(result)) xt_promise_observe_value(result);
  return result;
}

static xt_value xts_resolve(xt_value value) { return xt_promise_resolve(value); }
static xt_value xts_reject(xt_value error) { return xt_promise_reject(error); }

/** Reject an internally-owned promise (a reader/writer `closed`/`ready`) that
 * the program is not required to observe. Handlers still run; only the
 * unhandled-rejection report is suppressed. */
static void xts_reject_observed(xt_value promise, xt_value error) {
  if (!xt_is_promise(promise)) return;
  xt_promise_observe_value(promise);
  xt_promise_reject_value(promise, error);
}

/* Build a real Error-family value. `xt_error_from_message` strips the
 * "TypeError: " / "RangeError: " prefix from `.message` and gives the error the
 * matching prototype, which is what `catch` and `.name` should observe. The
 * call sites pass either a bare message or one that already carries the
 * prefix. */
static xt_value xts_error_with_prefix(const char *prefix, const char *message) {
  char buffer[512];
  size_t prefix_length = strlen(prefix);
  if (strncmp(message, prefix, prefix_length) == 0) {
    snprintf(buffer, sizeof(buffer), "%s", message);
  } else {
    snprintf(buffer, sizeof(buffer), "%s: %s", prefix, message);
  }
  return xt_error_from_message(buffer);
}

static xt_value xts_type_error(const char *message) {
  return xts_error_with_prefix("TypeError", message);
}

static xt_value xts_range_error(const char *message) {
  return xts_error_with_prefix("RangeError", message);
}

static xt_value xts_throw_type(const char *message) {
  xt_value error = xts_type_error(message);
  xt_throw(error);
  return error;
}

/** `{ value, done }`, the result shape shared by readers and async iterators. */
static xt_value xts_result(xt_value value, int done) {
  xt_value result = xt_object_new();
  xts_set(result, "value", value);
  xts_set(result, "done", xt_bool(done));
  return result;
}

/** A native closure with a copied environment. */
static xt_value xts_closure(void *fn, int32_t count, xt_value *environment) {
  return xt_closure_new(fn, count, environment);
}

/**
 * Call a user-supplied stream hook under a C exception boundary. A throwing
 * `start`/`pull`/`write`/`close` must error the stream (and reject the relevant
 * promise), not unwind through the constructor or `writer.write`.
 */
static int xts_try_call(xt_value fn, xt_value thisValue, int32_t argc, xt_value *argv,
                        xt_value *result, xt_value *error) {
  if (!XT_IS_FUNCTION(fn)) {
    *result = XT_UNDEFINED;
    return 0;
  }
  void *frame = xt_try_enter();
  if (xt_try_setjmp(frame) == 0) {
    *result = xt_call_with_this(fn, thisValue, argc, argv);
    xt_try_leave(frame);
    return 0;
  }
  *error = xt_try_exception(frame);
  xt_try_leave(frame);
  return 1;
}

/* -- strategy ------------------------------------------------------------- */

/** Read `highWaterMark` from a queuing strategy (default 1). */
static int xts_strategy_hwm(xt_value strategy) {
  if (XT_IS_OBJECT(strategy)) {
    xt_value hwm = xts_get(strategy, "highWaterMark");
    if (XT_IS_NUMBER(hwm)) {
      double value = xt_to_number(hwm);
      if (value >= 0 && value == value) return (int)value;
    }
  }
  return 1;
}

/** `strategy.size`, or `undefined` when the strategy has none. */
static xt_value xts_strategy_size(xt_value strategy) {
  if (XT_IS_OBJECT(strategy)) {
    xt_value size = xts_get(strategy, "size");
    if (XT_IS_FUNCTION(size)) return size;
  }
  return XT_UNDEFINED;
}

/* -- prototypes and cross-module entry points ----------------------------- */

xt_value xt_stream_readable_proto(void);
xt_value xt_stream_writable_proto(void);
xt_value xt_stream_transform_proto(void);
xt_value xt_stream_readable_controller_proto(void);
xt_value xt_stream_writable_controller_proto(void);
xt_value xt_stream_reader_proto(void);
xt_value xt_stream_writer_proto(void);
xt_value xt_stream_transform_controller_proto(void);
xt_value xt_stream_count_strategy_proto(void);
xt_value xt_stream_byte_strategy_proto(void);

xt_value xt_readable_stream_ctor(int32_t argc, xt_value *argv);
xt_value xt_writable_stream_ctor(int32_t argc, xt_value *argv);
xt_value xt_transform_stream_ctor(int32_t argc, xt_value *argv);
xt_value xt_count_queuing_strategy_ctor(int32_t argc, xt_value *argv);
xt_value xt_byte_length_queuing_strategy_ctor(int32_t argc, xt_value *argv);
xt_value xt_text_encoder_stream_ctor(int32_t argc, xt_value *argv);
xt_value xt_text_decoder_stream_ctor(int32_t argc, xt_value *argv);

/* `ReadableStream.from`, exported through the built-in constructor table. */
int xt_streams_has_static(const char *name, const char *method);
xt_value xt_streams_static(xt_value name, int32_t argc, xt_value *argv);
int xt_streams_instance_of(xt_value value, int32_t kind);

/* -- shared stream operations (implemented in the readable/writable parts) - */

static xt_value xts_readable_create(xt_value state, xt_value source, xt_value strategy);
static void xts_readable_pull_if_needed(xt_value state);
static void xts_readable_close(xt_value state);
static void xts_readable_error(xt_value state, xt_value error);
static xt_value xts_readable_controller_enqueue(xt_value state, xt_value chunk);
static xt_value xts_readable_controller_close(xt_value state);
static xt_value xts_readable_cancel(xt_value state, xt_value reason);
static xt_value xts_readable_reader_new(xt_value stream, xt_value state);

static xt_value xts_writable_create(xt_value state, xt_value sink, xt_value strategy);
static void xts_writable_process(xt_value state);
static void xts_writable_error(xt_value state, xt_value error);
static void xts_writable_close_step(xt_value state);
static xt_value xts_writable_abort(xt_value state, xt_value reason);
static xt_value xts_writable_writer_new(xt_value stream, xt_value state);

static xt_value xts_transform_create(xt_value transformer, xt_value readable_strategy,
                                     xt_value writable_strategy);

#endif /* XT_STREAMS_COMMON_INC */
