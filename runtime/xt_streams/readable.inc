/* WHATWG ReadableStream, its default reader, controller and helpers. */

#ifndef XT_STREAMS_READABLE_INC
#define XT_STREAMS_READABLE_INC

/* -- state helpers -------------------------------------------------------- */

static xt_value xts_readable_controller_of(xt_value state) { return xts_get(state, "controller"); }

static double xts_readable_total(xt_value state) {
  xt_value total = xts_get(state, "queueTotal");
  return XT_IS_NUMBER(total) ? xt_to_number(total) : 0.0;
}

static double xts_readable_desired_size(xt_value state) {
  int readable = xts_int(state, "state");
  if (readable == XT_STREAMS_RS_CLOSED) return 0.0;
  if (readable == XT_STREAMS_RS_ERRORED) return 0.0;
  double hwm = xt_to_number(xts_get(state, "hwm"));
  return hwm - xts_readable_total(state);
}

static int xts_readable_has_pending_reads(xt_value state) {
  xt_value reader = xts_get(state, "reader");
  if (!XT_IS_OBJECT(reader)) return 0;
  return xts_array_count(xts_get(reader, "requests")) > 0;
}

static xt_value xts_readable_closed_promise(xt_value state) {
  xt_value reader = xts_get(state, "reader");
  return XT_IS_OBJECT(reader) ? xts_get(reader, "closedPromise") : XT_UNDEFINED;
}

static void xts_readable_settle_requests(xt_value state, xt_value value, int done) {
  xt_value reader = xts_get(state, "reader");
  if (!XT_IS_OBJECT(reader)) return;
  xt_value requests = xts_get(reader, "requests");
  int count = xts_array_count(requests);
  for (int i = 0; i < count; i++) {
    xt_value request = xts_array_shift(requests);
    xt_promise_fulfill(request, xts_result(value, done));
  }
}

static void xts_readable_close(xt_value state) {
  if (!XT_IS_OBJECT(state) || xts_int(state, "state") != XT_STREAMS_RS_READABLE) return;
  xts_set(state, "state", xt_number((double)XT_STREAMS_RS_CLOSED));
  xts_set(state, "closeRequested", XT_TRUE);
  xts_set(state, "queue", xts_array());
  xts_set(state, "queueTotal", xt_number(0));
  xt_value closed = xts_readable_closed_promise(state);
  if (xt_is_promise(closed)) xt_promise_fulfill(closed, XT_UNDEFINED);
  xts_readable_settle_requests(state, XT_UNDEFINED, 1);
}

static void xts_readable_error(xt_value state, xt_value error) {
  if (!XT_IS_OBJECT(state) || xts_int(state, "state") == XT_STREAMS_RS_ERRORED) return;
  xts_set(state, "state", xt_number((double)XT_STREAMS_RS_ERRORED));
  xts_set(state, "storedError", error);
  xts_set(state, "queue", xts_array());
  xts_set(state, "queueTotal", xt_number(0));
  xt_value closed = xts_readable_closed_promise(state);
  xts_reject_observed(closed, error);
  xt_value reader = xts_get(state, "reader");
  if (XT_IS_OBJECT(reader)) {
    xt_value requests = xts_get(reader, "requests");
    int count = xts_array_count(requests);
    for (int i = 0; i < count; i++) {
      xt_value request = xts_array_shift(requests);
      xt_promise_reject_value(request, error);
    }
  }
}

/* -- pull ----------------------------------------------------------------- */

static int xts_readable_should_pull(xt_value state) {
  if (!xts_truthy_field(state, "started")) return 0;
  if (xts_int(state, "state") != XT_STREAMS_RS_READABLE) return 0;
  if (!xts_truthy_field(state, "locked")) return 0;
  xt_value reader = xts_get(state, "reader");
  if (!XT_IS_OBJECT(reader)) return 0;
  if (xts_readable_has_pending_reads(state)) return 1;
  return xts_readable_desired_size(state) > 0;
}

static void xts_readable_pull_done(xt_value state) {
  xts_set(state, "pulling", XT_FALSE);
  if (xts_truthy_field(state, "pullAgain")) {
    xts_set(state, "pullAgain", XT_FALSE);
    xts_readable_pull_if_needed(state);
  }
}

static xt_value xts_readable_pull_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xts_readable_pull_done(xt_closure_env(env, 0));
  return XT_UNDEFINED;
}

static xt_value xts_readable_pull_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xts_readable_error(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static void xts_readable_pull_if_needed(xt_value state) {
  if (!XT_IS_OBJECT(state)) return;
  if (!xts_readable_should_pull(state)) return;
  if (xts_truthy_field(state, "pulling")) {
    xts_set(state, "pullAgain", XT_TRUE);
    return;
  }
  xt_value source = xts_get(state, "source");
  if (!XT_IS_OBJECT(source)) return;
  xt_value pull = xts_get(source, "pull");
  if (!XT_IS_FUNCTION(pull)) return;

  xt_value controller = xts_readable_controller_of(state);
  xts_set(state, "pulling", XT_TRUE);
  xts_set(state, "pullAgain", XT_FALSE);
  xt_value args[1] = {controller};
  xt_value result = XT_UNDEFINED;
  xt_value error = XT_UNDEFINED;
  if (xts_try_call(pull, source, 1, args, &result, &error)) {
    xts_readable_error(state, error);
    return;
  }
  if (xt_is_promise(result)) {
    xt_value environment[1] = {state};
    xts_then(result, xts_closure((void *)xts_readable_pull_ok, 1, environment),
             xts_closure((void *)xts_readable_pull_fail, 1, environment));
    return;
  }
  xts_readable_pull_done(state);
}

/* -- controller ----------------------------------------------------------- */

static xt_value xts_readable_controller_enqueue(xt_value state, xt_value chunk) {
  if (!XT_IS_OBJECT(state) || xts_int(state, "state") != XT_STREAMS_RS_READABLE ||
      xts_truthy_field(state, "closeRequested")) {
    xts_throw_type("TypeError: Cannot enqueue on a closed or errored stream");
    return XT_UNDEFINED;
  }

  double size = 1.0;
  xt_value size_fn = xts_get(state, "sizeFn");
  if (XT_IS_FUNCTION(size_fn)) {
    xt_value args[1] = {chunk};
    xt_value measured = XT_UNDEFINED;
    xt_value thrown = XT_UNDEFINED;
    if (xts_try_call(size_fn, XT_UNDEFINED, 1, args, &measured, &thrown)) {
      xts_readable_error(state, thrown);
      xt_throw(thrown);
      return XT_UNDEFINED;
    }
    size = xt_to_number(measured);
    if (!(size >= 0) || size != size) {
      xt_value error = xts_range_error("RangeError: The queuing strategy size function returned an invalid size");
      xts_readable_error(state, error);
      xt_throw(error);
      return XT_UNDEFINED;
    }
  }

  if (xts_readable_has_pending_reads(state)) {
    xt_value reader = xts_get(state, "reader");
    xt_value request = xts_array_shift(xts_get(reader, "requests"));
    xt_promise_fulfill(request, xts_result(chunk, 0));
  } else {
    xt_value record = xt_object_new();
    xts_set(record, "value", chunk);
    xts_set(record, "size", xt_number(size));
    xt_array_push(xts_get(state, "queue"), record);
    xts_set(state, "queueTotal", xt_number(xts_readable_total(state) + size));
  }

  xts_readable_pull_if_needed(state);
  return XT_UNDEFINED;
}

static xt_value xts_readable_controller_close(xt_value state) {
  if (!XT_IS_OBJECT(state) || xts_int(state, "state") != XT_STREAMS_RS_READABLE ||
      xts_truthy_field(state, "closeRequested")) {
    xts_throw_type("TypeError: Cannot close a closed or errored stream");
    return XT_UNDEFINED;
  }
  xts_set(state, "closeRequested", XT_TRUE);
  if (xts_array_count(xts_get(state, "queue")) == 0) xts_readable_close(state);
  return XT_UNDEFINED;
}

static xt_value xts_readable_controller_error(xt_value state, xt_value error) {
  xts_readable_error(state, error);
  return XT_UNDEFINED;
}

/* -- construction --------------------------------------------------------- */

static xt_value xts_readable_start_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value state = xt_closure_env(env, 0);
  xts_set(state, "started", XT_TRUE);
  xts_readable_pull_if_needed(state);
  return XT_UNDEFINED;
}

static xt_value xts_readable_start_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xts_readable_error(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_readable_create(xt_value state, xt_value source, xt_value strategy) {
  xt_value stream = xt_object_new_with_proto(xt_stream_readable_proto());
  xt_value controller = xt_object_new_with_proto(xt_stream_readable_controller_proto());
  xts_set(controller, "__state", state);
  xts_set(stream, "__state", state);
  xts_set(state, "stream", stream);
  xts_set(state, "controller", controller);
  xts_set(state, "source", source);
  xts_set(state, "hwm", xt_number((double)xts_strategy_hwm(strategy)));
  xts_set(state, "sizeFn", xts_strategy_size(strategy));
  xts_set(state, "queue", xts_array());
  xts_set(state, "queueTotal", xt_number(0));
  xts_set(state, "closeRequested", XT_FALSE);
  xts_set(state, "started", XT_TRUE);
  xts_set(state, "pulling", XT_FALSE);
  xts_set(state, "pullAgain", XT_FALSE);

  xt_value start = XT_UNDEFINED;
  if (XT_IS_OBJECT(source)) start = xts_get(source, "start");
  if (XT_IS_FUNCTION(start)) {
    xts_set(state, "started", XT_FALSE);
    xt_value args[1] = {controller};
    xt_value result = XT_UNDEFINED;
    xt_value error = XT_UNDEFINED;
    if (xts_try_call(start, source, 1, args, &result, &error)) {
      xts_readable_error(state, error);
      return stream;
    }
    if (xt_is_promise(result)) {
      xt_value environment[1] = {state};
      xts_then(result, xts_closure((void *)xts_readable_start_ok, 1, environment),
               xts_closure((void *)xts_readable_start_fail, 1, environment));
    } else {
      xts_set(state, "started", XT_TRUE);
    }
  }
  return stream;
}

/* -- default reader ------------------------------------------------------- */

static int xts_reader_valid(xt_value self, xt_value state) {
  return XT_IS_OBJECT(state) && xts_get(state, "reader") == self;
}

static xt_value xts_reader_closed_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xts_get(self, "closedPromise");
}

static xt_value xts_reader_read(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!xts_reader_valid(self, state)) return xts_reject(xts_type_error("TypeError: Reader is not attached to this stream"));

  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_RS_ERRORED) return xts_reject(xts_get(state, "storedError"));
  if (stream_state == XT_STREAMS_RS_CLOSED) return xts_resolve(xts_result(XT_UNDEFINED, 1));

  xt_value queue = xts_get(state, "queue");
  if (xts_array_count(queue) > 0) {
    xt_value record = xts_array_shift(queue);
    double size = xt_to_number(xts_get(record, "size"));
    xts_set(state, "queueTotal", xt_number(xts_readable_total(state) - size));
    xt_value value = xts_get(record, "value");
    if (xts_truthy_field(state, "closeRequested") && xts_array_count(queue) == 0) {
      xts_readable_close(state);
    } else {
      xts_readable_pull_if_needed(state);
    }
    return xts_resolve(xts_result(value, 0));
  }

  xt_value promise = xt_promise_create();
  xt_array_push(xts_get(self, "requests"), promise);
  xts_readable_pull_if_needed(state);
  return promise;
}

static xt_value xts_reader_cancel(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!xts_reader_valid(self, state)) return xts_reject(xts_type_error("TypeError: Reader is not attached to this stream"));
  return xts_readable_cancel(state, xt_arg_at(argc, argv, 0));
}

static xt_value xts_reader_release(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state) || xts_get(state, "reader") != self) return XT_UNDEFINED;
  xt_value requests = xts_get(self, "requests");
  int count = xts_array_count(requests);
  if (count > 0) {
    xt_value error = xts_type_error("TypeError: Reader was released with pending read requests");
    for (int i = 0; i < count; i++) {
      xt_value request = xts_array_shift(requests);
      xt_promise_reject_value(request, error);
    }
    xts_reject_observed(xts_get(self, "closedPromise"), error);
  }
  xts_set(state, "locked", XT_FALSE);
  xts_set(state, "reader", XT_UNDEFINED);
  xts_set(self, "__released", XT_TRUE);
  return XT_UNDEFINED;
}

static xt_value xts_readable_reader_new(xt_value stream, xt_value state) {
  xt_value reader = xt_object_new_with_proto(xt_stream_reader_proto());
  xts_set(reader, "__state", state);
  xts_set(reader, "__stream", stream);
  xts_set(reader, "closedPromise", xt_promise_create());
  xts_set(reader, "requests", xts_array());
  xts_set(reader, "__released", XT_FALSE);
  xts_set(state, "locked", XT_TRUE);
  xts_set(state, "reader", reader);

  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_RS_CLOSED) {
    xt_promise_fulfill(xts_get(reader, "closedPromise"), XT_UNDEFINED);
  } else if (stream_state == XT_STREAMS_RS_ERRORED) {
    xts_reject_observed(xts_get(reader, "closedPromise"), xts_get(state, "storedError"));
  }
  return reader;
}

/* -- ReadableStream methods ----------------------------------------------- */

static xt_value xts_readable_locked_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  return xt_bool(XT_IS_OBJECT(state) && xts_truthy_field(state, "locked"));
}

static xt_value xts_readable_get_reader(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_truthy_field(state, "locked")) {
    xts_throw_type("TypeError: ReadableStream is already locked to a reader");
    return XT_UNDEFINED;
  }
  xt_value options = xt_arg_at(argc, argv, 0);
  if (XT_IS_OBJECT(options)) {
    xt_value mode = xts_get(options, "mode");
    if (XT_IS_STRING(mode) && strcmp(xt_string_data(mode), "byob") == 0) {
      xts_throw_type("TypeError: ReadableStreamBYOBReader is not supported");
      return XT_UNDEFINED;
    }
  }
  return xts_readable_reader_new(self, state);
}

static xt_value xts_readable_cancel(xt_value state, xt_value reason) {
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  int stream_state = xts_int(state, "state");
  if (stream_state == XT_STREAMS_RS_CLOSED) return xts_resolve(XT_UNDEFINED);
  if (stream_state == XT_STREAMS_RS_ERRORED) return xts_reject(xts_get(state, "storedError"));

  xts_set(state, "closeRequested", XT_TRUE);
  xts_set(state, "queue", xts_array());
  xts_set(state, "queueTotal", xt_number(0));
  xts_readable_close(state);

  xt_value source = xts_get(state, "source");
  xt_value cancel = XT_UNDEFINED;
  if (XT_IS_OBJECT(source)) cancel = xts_get(source, "cancel");
  if (XT_IS_FUNCTION(cancel)) {
    xt_value args[1] = {reason};
    xt_value result = XT_UNDEFINED;
    xt_value error = XT_UNDEFINED;
    if (xts_try_call(cancel, source, 1, args, &result, &error)) return xts_reject(error);
    if (xt_is_promise(result)) return result;
  }
  return xts_resolve(XT_UNDEFINED);
}

static xt_value xts_readable_method_cancel(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  if (xts_truthy_field(state, "locked")) {
    return xts_reject(xts_type_error("TypeError: Cannot cancel a locked stream"));
  }
  return xts_readable_cancel(state, xt_arg_at(argc, argv, 0));
}

#endif /* XT_STREAMS_READABLE_INC */
