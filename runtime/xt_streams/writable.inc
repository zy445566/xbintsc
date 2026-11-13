/* WHATWG WritableStream, its default writer, controller and helpers. */

#ifndef XT_STREAMS_WRITABLE_INC
#define XT_STREAMS_WRITABLE_INC

static double xts_writable_total(xt_value state) {
  xt_value total = xts_get(state, "queueTotal");
  return XT_IS_NUMBER(total) ? xt_to_number(total) : 0.0;
}

static double xts_writable_desired_size(xt_value state) {
  if (xts_int(state, "state") == XT_STREAMS_WS_ERRORED) return 0.0;
  if (xts_int(state, "state") == XT_STREAMS_WS_CLOSED) return 0.0;
  double hwm = xt_to_number(xts_get(state, "hwm"));
  return hwm - xts_writable_total(state);
}

static xt_value xts_writable_writer_of(xt_value state) { return xts_get(state, "writer"); }

static void xts_writable_settle_ready(xt_value state) {
  xt_value writer = xts_writable_writer_of(state);
  if (!XT_IS_OBJECT(writer)) return;
  xt_value ready = xts_get(writer, "readyPromise");
  if (!xt_is_promise(ready)) return;
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED) {
    if (xt_promise_state_of(ready) == XT_PROMISE_PENDING) {
      xts_reject_observed(ready, xts_get(state, "storedError"));
    }
    return;
  }
  if (xts_writable_desired_size(state) > 0) {
    if (xt_promise_state_of(ready) == XT_PROMISE_PENDING) xt_promise_fulfill(ready, XT_UNDEFINED);
  } else if (xt_promise_state_of(ready) != XT_PROMISE_PENDING) {
    xts_set(writer, "readyPromise", xt_promise_create());
  }
}

static void xts_writable_finish_close(xt_value state) {
  if (!XT_IS_OBJECT(state)) return;
  int current = xts_int(state, "state");
  if (current == XT_STREAMS_WS_CLOSED || current == XT_STREAMS_WS_ERRORED) return;
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_CLOSED));
  xts_set(state, "closeRequested", XT_TRUE);
  xt_value close = xts_get(state, "closePromise");
  if (xt_is_promise(close)) xt_promise_fulfill(close, XT_UNDEFINED);
  xt_value writer = xts_get(state, "writer");
  if (XT_IS_OBJECT(writer)) {
    xt_value closed = xts_get(writer, "closedPromise");
    if (xt_is_promise(closed)) xt_promise_fulfill(closed, XT_UNDEFINED);
    xt_value ready = xts_get(writer, "readyPromise");
    if (xt_is_promise(ready) && xt_promise_state_of(ready) == XT_PROMISE_PENDING) {
      xt_promise_fulfill(ready, XT_UNDEFINED);
    }
  }
}

static void xts_writable_error(xt_value state, xt_value error) {
  if (!XT_IS_OBJECT(state)) return;
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED || stream_state == XT_STREAMS_WS_CLOSED) return;
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_ERRORED));
  xts_set(state, "storedError", error);

  xt_value queue = xts_get(state, "queue");
  int count = xts_array_count(queue);
  for (int i = 0; i < count; i++) {
    xt_value record = xts_array_shift(queue);
    xt_value promise = xts_get(record, "promise");
    if (xt_is_promise(promise)) xt_promise_reject_value(promise, error);
  }
  xts_set(state, "queueTotal", xt_number(0));
  xts_set(state, "inFlight", XT_FALSE);

  xt_value close = xts_get(state, "closePromise");
  if (xt_is_promise(close) && xt_promise_state_of(close) == XT_PROMISE_PENDING) {
    xts_reject_observed(close, error);
  }
  xt_value writer = xts_get(state, "writer");
  if (XT_IS_OBJECT(writer)) {
    xts_reject_observed(xts_get(writer, "closedPromise"), error);
    xts_reject_observed(xts_get(writer, "readyPromise"), error);
  }
  /* A failing transform (or its flush) must also error the readable side so a
   * consumer awaiting `read()` observes the failure instead of hanging. */
  xt_value transform = xts_get(state, "transformState");
  if (XT_IS_OBJECT(transform)) {
    xts_readable_error(xts_get(transform, "readableState"), error);
  }
}

static void xts_writable_complete_write(xt_value state) {
  xt_value queue = xts_get(state, "queue");
  /* A synchronous sink may have errored the stream from inside `write`, which
   * clears the queue before this completion runs. */
  if (xts_array_count(queue) == 0) {
    xts_set(state, "inFlight", XT_FALSE);
    return;
  }
  xt_value record = xts_array_shift(queue);
  double size = xt_to_number(xts_get(record, "size"));
  xts_set(state, "queueTotal", xt_number(xts_writable_total(state) - size));
  xt_value promise = xts_get(record, "promise");
  if (xt_is_promise(promise)) xt_promise_fulfill(promise, XT_UNDEFINED);
  xts_set(state, "inFlight", XT_FALSE);
  xts_writable_settle_ready(state);
  xts_writable_process(state);
}

static xt_value xts_writable_write_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xts_writable_complete_write(xt_closure_env(env, 0));
  return XT_UNDEFINED;
}

static xt_value xts_writable_write_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value state = xt_closure_env(env, 0);
  xts_writable_error(state, xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_writable_close_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xts_writable_finish_close(xt_closure_env(env, 0));
  return XT_UNDEFINED;
}

static xt_value xts_writable_close_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xts_writable_error(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static void xts_writable_close_step(xt_value state) {
  if (!XT_IS_OBJECT(state)) return;
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_CLOSED || stream_state == XT_STREAMS_WS_ERRORED) return;
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_CLOSING));
  xt_value sink = xts_get(state, "sink");
  xt_value close = XT_IS_OBJECT(sink) ? xts_get(sink, "close") : XT_UNDEFINED;
  if (!XT_IS_FUNCTION(close)) {
    xts_writable_finish_close(state);
    return;
  }
  xt_value controller = xts_get(state, "controller");
  xt_value args[1] = {controller};
  xt_value result = XT_UNDEFINED;
  xt_value error = XT_UNDEFINED;
  if (xts_try_call(close, sink, 1, args, &result, &error)) {
    xts_writable_error(state, error);
    return;
  }
  if (xt_is_promise(result)) {
    xt_value environment[1] = {state};
    xts_then(result, xts_closure((void *)xts_writable_close_ok, 1, environment),
             xts_closure((void *)xts_writable_close_fail, 1, environment));
    return;
  }
  xts_writable_finish_close(state);
}

static void xts_writable_process(xt_value state) {
  if (!XT_IS_OBJECT(state)) return;
  if (!xts_truthy_field(state, "started")) return;
  if (xts_truthy_field(state, "inFlight")) return;
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED || stream_state == XT_STREAMS_WS_CLOSED) return;

  xt_value queue = xts_get(state, "queue");
  int count = xts_array_count(queue);
  if (count > 0) {
    xt_value record = xt_array_get(queue, xt_number(0));
    xts_set(state, "inFlight", XT_TRUE);
    xt_value sink = xts_get(state, "sink");
    xt_value write = XT_IS_OBJECT(sink) ? xts_get(sink, "write") : XT_UNDEFINED;
    if (!XT_IS_FUNCTION(write)) {
      xts_writable_complete_write(state);
      return;
    }
    xt_value controller = xts_get(state, "controller");
    xt_value args[2] = {xts_get(record, "chunk"), controller};
    xt_value result = XT_UNDEFINED;
    xt_value error = XT_UNDEFINED;
    if (xts_try_call(write, sink, 2, args, &result, &error)) {
      xts_set(state, "inFlight", XT_FALSE);
      xts_writable_error(state, error);
      return;
    }
    xt_value environment[1] = {state};
    if (xt_is_promise(result)) {
      xts_then(result, xts_closure((void *)xts_writable_write_ok, 1, environment),
               xts_closure((void *)xts_writable_write_fail, 1, environment));
    } else {
      xts_writable_complete_write(state);
    }
    return;
  }

  if (xts_truthy_field(state, "closeRequested") && stream_state != XT_STREAMS_WS_CLOSED) {
    xts_writable_close_step(state);
  }
}

static xt_value xts_writable_start_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value state = xt_closure_env(env, 0);
  xts_set(state, "started", XT_TRUE);
  xts_writable_settle_ready(state);
  xts_writable_process(state);
  return XT_UNDEFINED;
}

static xt_value xts_writable_start_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xts_writable_error(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_writable_create(xt_value state, xt_value sink, xt_value strategy) {
  xt_value stream = xt_object_new_with_proto(xt_stream_writable_proto());
  xt_value controller = xt_object_new_with_proto(xt_stream_writable_controller_proto());
  xts_set(controller, "__state", state);
  xts_set(stream, "__state", state);
  xts_set(state, "stream", stream);
  xts_set(state, "controller", controller);
  xts_set(state, "sink", sink);
  xts_set(state, "hwm", xt_number((double)xts_strategy_hwm(strategy)));
  xts_set(state, "sizeFn", xts_strategy_size(strategy));
  xts_set(state, "queue", xts_array());
  xts_set(state, "queueTotal", xt_number(0));
  xts_set(state, "inFlight", XT_FALSE);
  xts_set(state, "closeRequested", XT_FALSE);
  xts_set(state, "closePromise", xt_promise_create());
  xts_set(state, "started", XT_TRUE);

  xt_value start = XT_UNDEFINED;
  if (XT_IS_OBJECT(sink)) start = xts_get(sink, "start");
  if (XT_IS_FUNCTION(start)) {
    xts_set(state, "started", XT_FALSE);
    xt_value args[1] = {controller};
    xt_value result = XT_UNDEFINED;
    xt_value error = XT_UNDEFINED;
    if (xts_try_call(start, sink, 1, args, &result, &error)) {
      xts_writable_error(state, error);
      return stream;
    }
    if (xt_is_promise(result)) {
      xt_value environment[1] = {state};
      xts_then(result, xts_closure((void *)xts_writable_start_ok, 1, environment),
               xts_closure((void *)xts_writable_start_fail, 1, environment));
    } else {
      xts_set(state, "started", XT_TRUE);
    }
  }
  return stream;
}

/* -- default writer ------------------------------------------------------- */

static int xts_writer_valid(xt_value self, xt_value state) {
  return XT_IS_OBJECT(state) && xts_get(state, "writer") == self;
}

static xt_value xts_writer_closed_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xts_get(self, "closedPromise");
}

static xt_value xts_writer_ready_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xts_get(self, "readyPromise");
}

static xt_value xts_writer_desired_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_int(state, "state") == XT_STREAMS_WS_ERRORED) return XT_NULL;
  return xt_number(xts_writable_desired_size(state));
}

static xt_value xts_writer_write(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!xts_writer_valid(self, state)) {
    return xts_reject(xts_type_error("TypeError: Writer is not attached to this stream"));
  }
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED) return xts_reject(xts_get(state, "storedError"));
  if (stream_state != XT_STREAMS_WS_WRITABLE) {
    return xts_reject(xts_type_error("TypeError: Cannot write to a closing or closed stream"));
  }

  xt_value chunk = xt_arg_at(argc, argv, 0);
  double size = 1.0;
  xt_value size_fn = xts_get(state, "sizeFn");
  if (XT_IS_FUNCTION(size_fn)) {
    xt_value args[1] = {chunk};
    xt_value measured = XT_UNDEFINED;
    xt_value thrown = XT_UNDEFINED;
    if (xts_try_call(size_fn, XT_UNDEFINED, 1, args, &measured, &thrown)) {
      xts_writable_error(state, thrown);
      return xts_reject(thrown);
    }
    size = xt_to_number(measured);
    if (!(size >= 0) || size != size) {
      xt_value error = xts_range_error("RangeError: The queuing strategy size function returned an invalid size");
      xts_writable_error(state, error);
      return xts_reject(error);
    }
  }

  xt_value promise = xt_promise_create();
  xt_value record = xt_object_new();
  xts_set(record, "chunk", chunk);
  xts_set(record, "size", xt_number(size));
  xts_set(record, "promise", promise);
  xt_array_push(xts_get(state, "queue"), record);
  xts_set(state, "queueTotal", xt_number(xts_writable_total(state) + size));
  xts_writable_settle_ready(state);
  xts_writable_process(state);
  return promise;
}

static xt_value xts_writer_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!xts_writer_valid(self, state)) {
    return xts_reject(xts_type_error("TypeError: Writer is not attached to this stream"));
  }
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED) return xts_reject(xts_get(state, "storedError"));
  if (stream_state == XT_STREAMS_WS_CLOSED) return xts_resolve(XT_UNDEFINED);
  if (xts_truthy_field(state, "closeRequested")) return xts_get(state, "closePromise");

  xts_set(state, "closeRequested", XT_TRUE);
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_CLOSING));
  xts_writable_process(state);
  return xts_get(state, "closePromise");
}

static xt_value xts_writer_abort(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!xts_writer_valid(self, state)) {
    return xts_reject(xts_type_error("TypeError: Writer is not attached to this stream"));
  }
  return xts_writable_abort(state, xt_arg_at(argc, argv, 0));
}

static xt_value xts_writer_release(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state) || xts_get(state, "writer") != self) return XT_UNDEFINED;
  xts_set(state, "locked", XT_FALSE);
  xts_set(state, "writer", XT_UNDEFINED);
  xts_set(self, "__released", XT_TRUE);
  return XT_UNDEFINED;
}

static xt_value xts_writable_writer_new(xt_value stream, xt_value state) {
  xt_value writer = xt_object_new_with_proto(xt_stream_writer_proto());
  xts_set(writer, "__state", state);
  xts_set(writer, "__stream", stream);
  xts_set(writer, "closedPromise", xt_promise_create());
  xts_set(writer, "readyPromise", xt_promise_resolve(XT_UNDEFINED));
  xts_set(writer, "__released", XT_FALSE);
  xts_set(state, "locked", XT_TRUE);
  xts_set(state, "writer", writer);

  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_CLOSED) {
    xt_promise_fulfill(xts_get(writer, "closedPromise"), XT_UNDEFINED);
  } else if (stream_state == XT_STREAMS_WS_ERRORED) {
    xt_value error = xts_get(state, "storedError");
    xts_reject_observed(xts_get(writer, "closedPromise"), error);
    xts_reject_observed(xts_get(writer, "readyPromise"), error);
  }
  return writer;
}

/* -- WritableStream methods ----------------------------------------------- */

static xt_value xts_writable_locked_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  return xt_bool(XT_IS_OBJECT(state) && xts_truthy_field(state, "locked"));
}

static xt_value xts_writable_get_writer(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_truthy_field(state, "locked")) {
    xts_throw_type("TypeError: WritableStream is already locked to a writer");
    return XT_UNDEFINED;
  }
  return xts_writable_writer_new(self, state);
}

static xt_value xts_writable_abort(xt_value state, xt_value reason) {
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_CLOSED) return xts_resolve(XT_UNDEFINED);
  if (stream_state == XT_STREAMS_WS_ERRORED) return xts_reject(xts_get(state, "storedError"));

  xt_value queue = xts_get(state, "queue");
  int count = xts_array_count(queue);
  for (int i = 0; i < count; i++) {
    xt_value record = xts_array_shift(queue);
    xt_value promise = xts_get(record, "promise");
    if (xt_is_promise(promise)) xt_promise_reject_value(promise, reason);
  }
  xts_set(state, "queueTotal", xt_number(0));
  xts_set(state, "inFlight", XT_FALSE);
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_CLOSED));
  xts_set(state, "closeRequested", XT_TRUE);

  xt_value close = xts_get(state, "closePromise");
  if (xt_is_promise(close) && xt_promise_state_of(close) == XT_PROMISE_PENDING) {
    xts_reject_observed(close, reason);
  }
  xt_value writer = xts_get(state, "writer");
  if (XT_IS_OBJECT(writer)) {
    xts_reject_observed(xts_get(writer, "closedPromise"), reason);
    xts_reject_observed(xts_get(writer, "readyPromise"), reason);
  }

  xt_value sink = xts_get(state, "sink");
  xt_value abort = XT_IS_OBJECT(sink) ? xts_get(sink, "abort") : XT_UNDEFINED;
  if (XT_IS_FUNCTION(abort)) {
    xt_value controller = xts_get(state, "controller");
    xt_value args[2] = {reason, controller};
    xt_value result = XT_UNDEFINED;
    xt_value error = XT_UNDEFINED;
    if (xts_try_call(abort, sink, 2, args, &result, &error)) return xts_reject(error);
    if (xt_is_promise(result)) return result;
  }
  return xts_resolve(XT_UNDEFINED);
}

static xt_value xts_writable_method_abort(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  if (xts_truthy_field(state, "locked")) {
    return xts_reject(xts_type_error("TypeError: Cannot abort a locked stream"));
  }
  return xts_writable_abort(state, xt_arg_at(argc, argv, 0));
}

static xt_value xts_writable_method_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  if (xts_truthy_field(state, "locked")) {
    return xts_reject(xts_type_error("TypeError: Cannot close a locked stream"));
  }
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_WS_ERRORED) return xts_reject(xts_get(state, "storedError"));
  if (stream_state == XT_STREAMS_WS_CLOSED) return xts_resolve(XT_UNDEFINED);
  if (xts_truthy_field(state, "closeRequested")) return xts_get(state, "closePromise");
  xts_set(state, "closeRequested", XT_TRUE);
  xts_set(state, "state", xt_number((double)XT_STREAMS_WS_CLOSING));
  xts_writable_process(state);
  return xts_get(state, "closePromise");
}

/* -- controller methods --------------------------------------------------- */

static xt_value xts_writable_controller_error(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xts_writable_error(xts_get(self, "__state"), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

/* -- prototype ------------------------------------------------------------ */

xt_value xt_stream_writable_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("locked"),
                          xts_closure((void *)xts_writable_locked_get, 0, NULL));
  xts_set(proto, "getWriter", xts_closure((void *)xts_writable_get_writer, 0, NULL));
  xts_set(proto, "abort", xts_closure((void *)xts_writable_method_abort, 0, NULL));
  xts_set(proto, "close", xts_closure((void *)xts_writable_method_close, 0, NULL));
  return proto;
}

xt_value xt_stream_writer_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("closed"),
                          xts_closure((void *)xts_writer_closed_get, 0, NULL));
  xt_object_define_getter(proto, xt_string_from_cstr("ready"),
                          xts_closure((void *)xts_writer_ready_get, 0, NULL));
  xt_object_define_getter(proto, xt_string_from_cstr("desiredSize"),
                          xts_closure((void *)xts_writer_desired_get, 0, NULL));
  xts_set(proto, "write", xts_closure((void *)xts_writer_write, 0, NULL));
  xts_set(proto, "close", xts_closure((void *)xts_writer_close, 0, NULL));
  xts_set(proto, "abort", xts_closure((void *)xts_writer_abort, 0, NULL));
  xts_set(proto, "releaseLock", xts_closure((void *)xts_writer_release, 0, NULL));
  return proto;
}

xt_value xt_stream_writable_controller_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xts_set(proto, "error", xts_closure((void *)xts_writable_controller_error, 0, NULL));
  return proto;
}

#endif /* XT_STREAMS_WRITABLE_INC */
