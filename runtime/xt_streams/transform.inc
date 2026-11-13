/* WHATWG TransformStream and its controller. */

#ifndef XT_STREAMS_TRANSFORM_INC
#define XT_STREAMS_TRANSFORM_INC

static xt_value xts_transform_readable_state(xt_value state) { return xts_get(state, "readableState"); }

static xt_value xts_transform_writable_state(xt_value state) { return xts_get(state, "writableState"); }

static xt_value xts_transform_controller_of(xt_value state) { return xts_get(state, "controller"); }

static xt_value xts_transform_close_readable(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value state = xt_closure_env(env, 0);
  xt_value readable = xts_transform_readable_state(state);
  if (XT_IS_OBJECT(readable) && xts_int(readable, "state") == XT_STREAMS_RS_READABLE &&
      !xts_truthy_field(readable, "closeRequested")) {
    xts_readable_controller_close(readable);
  }
  return XT_UNDEFINED;
}

static xt_value xts_transform_sink_start(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value state = xt_closure_env(env, 0);
  xt_value transformer = xts_get(state, "transformer");
  if (!XT_IS_OBJECT(transformer)) return XT_UNDEFINED;
  xt_value start = xts_get(transformer, "start");
  if (!XT_IS_FUNCTION(start)) return XT_UNDEFINED;
  xt_value controller = xts_transform_controller_of(state);
  xt_value args[1] = {controller};
  return xt_call_with_this(start, transformer, 1, args);
}

static xt_value xts_transform_sink_write(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  xt_value state = xt_closure_env(env, 0);
  xt_value chunk = xt_arg_at(argc, argv, 0);
  xt_value transformer = xts_get(state, "transformer");
  xt_value transform = XT_IS_OBJECT(transformer) ? xts_get(transformer, "transform") : XT_UNDEFINED;
  xt_value controller = xts_transform_controller_of(state);

  if (!XT_IS_FUNCTION(transform)) {
    xts_readable_controller_enqueue(xts_transform_readable_state(state), chunk);
    return XT_UNDEFINED;
  }
  xt_value args[2] = {chunk, controller};
  return xt_call_with_this(transform, transformer, 2, args);
}

static xt_value xts_transform_sink_close(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  (void)argv;
  xt_value state = xt_closure_env(env, 0);
  xt_value transformer = xts_get(state, "transformer");
  xt_value flush = XT_IS_OBJECT(transformer) ? xts_get(transformer, "flush") : XT_UNDEFINED;
  if (!XT_IS_FUNCTION(flush)) {
    xts_transform_close_readable(XT_UNDEFINED, env, 0, NULL);
    return XT_UNDEFINED;
  }
  xt_value controller = xts_transform_controller_of(state);
  xt_value args[1] = {controller};
  xt_value result = xt_call_with_this(flush, transformer, 1, args);
  if (xt_is_promise(result)) {
    xt_value environment[1] = {state};
    return xts_then(result, xts_closure((void *)xts_transform_close_readable, 1, environment), XT_UNDEFINED);
  }
  xts_transform_close_readable(XT_UNDEFINED, env, 0, NULL);
  return XT_UNDEFINED;
}

static xt_value xts_transform_sink_abort(xt_value thisValue, xt_value env, int32_t argc, xt_value *argv) {
  (void)thisValue;
  (void)argc;
  xt_value state = xt_closure_env(env, 0);
  xt_value readable = xts_transform_readable_state(state);
  xts_readable_error(readable, xt_arg_at(argc, argv, 0));
  return XT_UNDEFINED;
}

static xt_value xts_transform_controller_enqueue(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  xt_value readable = xts_transform_readable_state(state);
  return xts_readable_controller_enqueue(readable, xt_arg_at(argc, argv, 0));
}

static xt_value xts_transform_controller_error(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  xt_value state = xts_get(self, "__state");
  xt_value error = xt_arg_at(argc, argv, 0);
  xts_readable_error(xts_transform_readable_state(state), error);
  xts_writable_error(xts_transform_writable_state(state), error);
  return XT_UNDEFINED;
}

static xt_value xts_transform_controller_terminate(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  xt_value readable = xts_transform_readable_state(state);
  if (XT_IS_OBJECT(readable) && xts_int(readable, "state") == XT_STREAMS_RS_READABLE &&
      !xts_truthy_field(readable, "closeRequested")) {
    xts_readable_controller_close(readable);
  }
  xt_value writable = xts_transform_writable_state(state);
  if (XT_IS_OBJECT(writable) && xts_int(writable, "state") != XT_STREAMS_WS_ERRORED) {
    xts_writable_error(writable, xts_type_error("TypeError: The transform stream was terminated"));
  }
  return XT_UNDEFINED;
}

static xt_value xts_transform_controller_desired_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_value state = xts_get(self, "__state");
  xt_value readable = xts_transform_readable_state(state);
  if (!XT_IS_OBJECT(readable)) return XT_UNDEFINED;
  if (xts_int(readable, "state") == XT_STREAMS_RS_ERRORED) return XT_NULL;
  return xt_number(xts_readable_desired_size(readable));
}

static xt_value xts_transform_create(xt_value transformer, xt_value readable_strategy,
                                     xt_value writable_strategy) {
  if (!XT_IS_OBJECT(transformer)) transformer = xt_object_new();
  xt_value state = xt_object_new();
  xts_set(state, "kind", xt_number((double)XT_STREAMS_TRANSFORM));
  xts_set(state, "transformer", transformer);

  xt_value controller = xt_object_new_with_proto(xt_stream_transform_controller_proto());
  xts_set(controller, "__state", state);
  xts_set(state, "controller", controller);

  xt_value readable_state = xts_state_new(XT_STREAMS_READABLE);
  xt_value writable_state = xts_state_new(XT_STREAMS_WRITABLE);
  xts_set(state, "readableState", readable_state);
  xts_set(state, "writableState", writable_state);
  /* Let `xts_writable_error` propagate a transform failure to the readable
   * side as well. */
  xts_set(writable_state, "transformState", state);

  /* TransformStream's default readable high-water mark is 0: the transformer
   * enqueues on demand instead of buffering ahead. */
  xt_value readable_options = readable_strategy;
  if (!XT_IS_OBJECT(readable_options)) {
    readable_options = xt_object_new();
    xts_set(readable_options, "highWaterMark", xt_number(0));
  }
  xt_value readable = xts_readable_create(readable_state, XT_UNDEFINED, readable_options);

  xt_value sink = xt_object_new();
  xt_value environment[1] = {state};
  xts_set(sink, "start", xts_closure((void *)xts_transform_sink_start, 1, environment));
  xts_set(sink, "write", xts_closure((void *)xts_transform_sink_write, 1, environment));
  xts_set(sink, "close", xts_closure((void *)xts_transform_sink_close, 1, environment));
  xts_set(sink, "abort", xts_closure((void *)xts_transform_sink_abort, 1, environment));
  xt_value writable = xts_writable_create(writable_state, sink, writable_strategy);

  xt_value transform = xt_object_new_with_proto(xt_stream_transform_proto());
  xts_set(transform, "__state", state);
  xts_set(transform, "readable", readable);
  xts_set(transform, "writable", writable);
  xts_set(state, "stream", transform);
  return transform;
}

xt_value xt_stream_transform_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  return proto;
}

xt_value xt_stream_transform_controller_proto(void) {
  static xt_value proto = XT_UNDEFINED;
  if (XT_IS_OBJECT(proto)) return proto;
  proto = xt_object_new();
  xt_gc_add_root(&proto);
  xt_object_define_getter(proto, xt_string_from_cstr("desiredSize"),
                          xts_closure((void *)xts_transform_controller_desired_get, 0, NULL));
  xts_set(proto, "enqueue", xts_closure((void *)xts_transform_controller_enqueue, 0, NULL));
  xts_set(proto, "error", xts_closure((void *)xts_transform_controller_error, 0, NULL));
  xts_set(proto, "terminate", xts_closure((void *)xts_transform_controller_terminate, 0, NULL));
  return proto;
}

#endif /* XT_STREAMS_TRANSFORM_INC */
