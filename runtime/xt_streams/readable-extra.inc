#ifndef XT_STREAMS_READABLE_EXTRA_INC
#define XT_STREAMS_READABLE_EXTRA_INC

/* -- tee ------------------------------------------------------------------ */

static xt_value xts_tee_step(xt_value pipe);

static xt_value xts_tee_enqueue_branch(xt_value branch_state, xt_value value) {
  if (!XT_IS_OBJECT(branch_state)) return XT_UNDEFINED;
  if (xts_int(branch_state, "state") != XT_STREAMS_RS_READABLE) return XT_UNDEFINED;
  if (xts_truthy_field(branch_state, "closeRequested")) return XT_UNDEFINED;
  return xts_readable_controller_enqueue(branch_state, value);
}

static xt_value xts_tee_finish(xt_value tee) {
  for (int i = 0; i < 2; i++) {
    xt_value branch = xts_get(tee, i == 0 ? "first" : "second");
    if (XT_IS_OBJECT(branch) && xts_int(branch, "state") == XT_STREAMS_RS_READABLE) {
      xts_readable_controller_close(branch);
    }
  }
  return XT_UNDEFINED;
}

static xt_value xts_tee_on_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value tee = xt_closure_env(env, 0);
  xt_value result = xt_arg_at(argc, argv, 0);
  if (xt_truthy(xts_get(result, "done"))) {
    xts_tee_finish(tee);
    return XT_UNDEFINED;
  }
  xt_value value = xts_get(result, "value");
  xts_tee_enqueue_branch(xts_get(tee, "first"), value);
  xts_tee_enqueue_branch(xts_get(tee, "second"), value);
  xts_tee_step(tee);
  return XT_UNDEFINED;
}

static xt_value xts_tee_on_error(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value tee = xt_closure_env(env, 0);
  xt_value error = xt_arg_at(argc, argv, 0);
  xts_readable_error(xts_get(tee, "first"), error);
  xts_readable_error(xts_get(tee, "second"), error);
  return XT_UNDEFINED;
}

static xt_value xts_tee_step(xt_value tee) {
  xt_value reader = xts_get(tee, "reader");
  xt_value promise = xt_call_method(reader, xt_string_from_cstr("read"), 0, NULL);
  xt_value environment[1] = {tee};
  xts_then(promise, xts_closure((void *)xts_tee_on_read, 1, environment),
           xts_closure((void *)xts_tee_on_error, 1, environment));
  return XT_UNDEFINED;
}

static xt_value xts_readable_method_tee(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_truthy_field(state, "locked")) {
    xts_throw_type("TypeError: Cannot tee a locked stream");
    return XT_UNDEFINED;
  }

  xt_value reader = xts_readable_reader_new(self, state);

  xt_value first_state = xts_state_new(XT_STREAMS_READABLE);
  xt_value second_state = xts_state_new(XT_STREAMS_READABLE);
  xts_set(first_state, "stream", XT_UNDEFINED);
  xts_set(second_state, "stream", XT_UNDEFINED);
  xt_value first = xts_readable_create(first_state, XT_UNDEFINED, XT_UNDEFINED);
  xt_value second = xts_readable_create(second_state, XT_UNDEFINED, XT_UNDEFINED);

  xt_value tee = xt_object_new();
  xts_set(tee, "reader", reader);
  xts_set(tee, "first", first_state);
  xts_set(tee, "second", second_state);
  xts_tee_step(tee);

  xt_value branches = xt_array_new(0, NULL);
  xt_array_push(branches, first);
  xt_array_push(branches, second);
  return branches;
}

/* -- pipeTo / pipeThrough ------------------------------------------------- */

static xt_value xts_pipe_resolve(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_promise_fulfill(xt_closure_env(env, 0), XT_UNDEFINED);
  return XT_UNDEFINED;
}

static xt_value xts_pipe_reject(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xt_promise_reject_value(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_step(xt_value pipe);

static xt_value xts_pipe_abort(xt_value pipe, xt_value reason) {
  xt_value writer = xts_get(pipe, "writer");
  xt_value promise = xt_call_method(writer, xt_string_from_cstr("abort"), 1, &reason);
  xt_value environment[1] = {xts_get(pipe, "result")};
  xts_then(promise, xts_closure((void *)xts_pipe_reject, 1, environment),
           xts_closure((void *)xts_pipe_reject, 1, environment));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_on_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xts_pipe_step(xt_closure_env(env, 0));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_write_failed(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xts_pipe_abort(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_on_read(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value pipe = xt_closure_env(env, 0);
  xt_value result = xt_arg_at(argc, argv, 0);
  if (xt_truthy(xts_get(result, "done"))) {
    xt_value writer = xts_get(pipe, "writer");
    xt_value promise = xt_call_method(writer, xt_string_from_cstr("close"), 0, NULL);
    xt_value environment[1] = {xts_get(pipe, "result")};
    xts_then(promise, xts_closure((void *)xts_pipe_resolve, 1, environment),
             xts_closure((void *)xts_pipe_reject, 1, environment));
    return XT_UNDEFINED;
  }
  xt_value writer = xts_get(pipe, "writer");
  xt_value value = xts_get(result, "value");
  xt_value promise = xt_call_method(writer, xt_string_from_cstr("write"), 1, &value);
  xt_value environment[1] = {pipe};
  xts_then(promise, xts_closure((void *)xts_pipe_on_write, 1, environment),
           xts_closure((void *)xts_pipe_write_failed, 1, environment));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_read_failed(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xts_pipe_abort(xt_closure_env(env, 0), xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_pipe_step(xt_value pipe) {
  xt_value reader = xts_get(pipe, "reader");
  xt_value promise = xt_call_method(reader, xt_string_from_cstr("read"), 0, NULL);
  xt_value environment[1] = {pipe};
  xts_then(promise, xts_closure((void *)xts_pipe_on_read, 1, environment),
           xts_closure((void *)xts_pipe_read_failed, 1, environment));
  return XT_UNDEFINED;
}

static xt_value xts_readable_method_pipe_to(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return xts_reject(xts_type_error("TypeError: Invalid stream"));
  if (xts_truthy_field(state, "locked")) {
    return xts_reject(xts_type_error("TypeError: Cannot pipe a locked stream"));
  }
  xt_value destination = xt_arg_at(argc, argv, 0);
  if (!XT_IS_OBJECT(destination)) {
    return xts_reject(xts_type_error("TypeError: pipeTo destination is not a WritableStream"));
  }

  xt_value reader = xts_readable_reader_new(self, state);
  xt_value writer = xt_call_method(destination, xt_string_from_cstr("getWriter"), 0, NULL);
  xt_value result = xt_promise_create();

  xt_value pipe = xt_object_new();
  xts_set(pipe, "reader", reader);
  xts_set(pipe, "writer", writer);
  xts_set(pipe, "result", result);
  xts_pipe_step(pipe);
  return result;
}

static xt_value xts_readable_method_pipe_through(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value transform = xt_arg_at(argc, argv, 0);
  if (!XT_IS_OBJECT(transform)) {
    xts_throw_type("TypeError: pipeThrough expects a TransformStream");
    return XT_UNDEFINED;
  }
  xt_value writable = xts_get(transform, "writable");
  xt_value readable = xts_get(transform, "readable");
  if (!XT_IS_OBJECT(writable) || !XT_IS_OBJECT(readable)) {
    xts_throw_type("TypeError: pipeThrough expects a TransformStream");
    return XT_UNDEFINED;
  }
  xt_value args[1] = {writable};
  xt_value promise = xts_readable_method_pipe_to(self, XT_UNDEFINED, 1, args);
  xt_promise_observe_value(promise);
  return readable;
}

/* -- async iteration ------------------------------------------------------ */

static xt_value xts_iterator_next_ok(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xt_value iterator = xt_closure_env(env, 0);
  xt_value result = xt_arg_at(argc, argv, 0);
  if (xt_truthy(xts_get(result, "done"))) {
    xts_set(iterator, "done", XT_TRUE);
    return xts_result(XT_UNDEFINED, 1);
  }
  return xts_result(xts_get(result, "value"), 0);
}

static xt_value xts_iterator_next_fail(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)env;
  (void)argc;
  xt_throw(xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_iterator_next(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  if (xts_truthy_field(self, "done")) return xts_resolve(xts_result(XT_UNDEFINED, 1));
  xt_value reader = xts_get(self, "reader");
  xt_value promise = xt_call_method(reader, xt_string_from_cstr("read"), 0, NULL);
  xt_value environment[1] = {self};
  return xts_then(promise, xts_closure((void *)xts_iterator_next_ok, 1, environment),
                  xts_closure((void *)xts_iterator_next_fail, 1, environment));
}

static xt_value xts_iterator_return(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value value = xt_arg_at(argc, argv, 0);
  if (!xts_truthy_field(self, "done")) {
    xts_set(self, "done", XT_TRUE);
    xt_value reader = xts_get(self, "reader");
    if (!xts_truthy_field(self, "preventCancel")) {
      xt_value promise = xt_call_method(reader, xt_string_from_cstr("cancel"), 1, &value);
      xt_promise_observe_value(promise);
    }
    xt_call_method(reader, xt_string_from_cstr("releaseLock"), 0, NULL);
  }
  return xts_resolve(xts_result(value, 1));
}

static xt_value xts_iterator_self(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return self;
}

static xt_value xts_readable_method_values(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_truthy_field(state, "locked")) {
    xts_throw_type("TypeError: ReadableStream is already locked to a reader");
    return XT_UNDEFINED;
  }
  xt_value reader = xts_readable_reader_new(self, state);
  xt_value iterator = xt_object_new();
  xts_set(iterator, "reader", reader);
  xts_set(iterator, "done", XT_FALSE);
  xt_value options = xt_arg_at(argc, argv, 0);
  int preventCancel = 0;
  if (XT_IS_OBJECT(options)) preventCancel = xts_truthy_field(options, "preventCancel");
  xts_set(iterator, "preventCancel", xt_bool(preventCancel));
  xts_set(iterator, "next", xts_closure((void *)xts_iterator_next, 0, NULL));
  xts_set(iterator, "return", xts_closure((void *)xts_iterator_return, 0, NULL));
  xt_object_set(iterator, xt_symbol_well_known("asyncIterator"),
                xts_closure((void *)xts_iterator_self, 0, NULL));
  return iterator;
}

static xt_value xts_readable_method_async_iterator(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xts_readable_method_values(self, XT_UNDEFINED, 0, NULL);
}

/* -- controller methods --------------------------------------------------- */

static xt_value xts_controller_desired_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  if (!XT_IS_OBJECT(state)) return XT_UNDEFINED;
  if (xts_int(state, "state") == XT_STREAMS_RS_ERRORED) return XT_NULL;
  return xt_number(xts_readable_desired_size(state));
}

static xt_value xts_controller_enqueue_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xts_readable_controller_enqueue(xts_get(self, "__state"), xt_arg_at(argc, argv, 0));
}

static xt_value xts_controller_close_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  return xts_readable_controller_close(xts_get(self, "__state"));
}

static xt_value xts_controller_error_method(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  return xts_readable_controller_error(xts_get(self, "__state"), xt_arg_at(argc, argv, 0));
}

/* -- prototype ------------------------------------------------------------ */

xt_value xt_stream_readable_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("locked"),
                          xts_closure((void *)xts_readable_locked_get, 0, NULL));
  xts_set(proto, "getReader", xts_closure((void *)xts_readable_get_reader, 0, NULL));
  xts_set(proto, "cancel", xts_closure((void *)xts_readable_method_cancel, 0, NULL));
  xts_set(proto, "tee", xts_closure((void *)xts_readable_method_tee, 0, NULL));
  xts_set(proto, "pipeTo", xts_closure((void *)xts_readable_method_pipe_to, 0, NULL));
  xts_set(proto, "pipeThrough", xts_closure((void *)xts_readable_method_pipe_through, 0, NULL));
  xts_set(proto, "values", xts_closure((void *)xts_readable_method_values, 0, NULL));
  xt_object_set(proto, xt_symbol_well_known("asyncIterator"),
                xts_closure((void *)xts_readable_method_async_iterator, 0, NULL));
  return proto;
}

xt_value xt_stream_reader_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("closed"),
                          xts_closure((void *)xts_reader_closed_get, 0, NULL));
  xts_set(proto, "read", xts_closure((void *)xts_reader_read, 0, NULL));
  xts_set(proto, "cancel", xts_closure((void *)xts_reader_cancel, 0, NULL));
  xts_set(proto, "releaseLock", xts_closure((void *)xts_reader_release, 0, NULL));
  return proto;
}

xt_value xt_stream_readable_controller_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("desiredSize"),
                          xts_closure((void *)xts_controller_desired_get, 0, NULL));
  xts_set(proto, "enqueue", xts_closure((void *)xts_controller_enqueue_method, 0, NULL));
  xts_set(proto, "close", xts_closure((void *)xts_controller_close_method, 0, NULL));
  xts_set(proto, "error", xts_closure((void *)xts_controller_error_method, 0, NULL));
  return proto;
}

#endif /* XT_STREAMS_READABLE_EXTRA_INC */
