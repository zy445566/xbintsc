/*
 * xbintsc runtime — `MessageChannel` / `MessagePort`.
 *
 * Both worker APIs expose the same channel:
 *
 *   const { port1, port2 } = new MessageChannel();
 *   port1.onmessage = (event) => ...;     // Web Worker
 *   port2.on("message", (value) => ...);  // node:worker_threads
 *   port2.postMessage(value);
 *
 * Delivery is asynchronous (a zero-delay timer), like the platform API, and
 * every value is structured-cloned on the way in.
 *
 * A port is local to the process. Sending a port to a worker (`postMessage`
 * with a transfer list) is not supported: the codec rejects a MessagePort
 * instead of silently copying an object that would never deliver. The transfer
 * list argument itself is accepted and the value is copied, exactly like
 * `structuredClone`.
 */

#include "worker_internal.h"

#include <stdlib.h>
#include <string.h>

static xt_value xt_wrk_str(const char *text) { return xt_string_from_cstr(text); }

static void xt_wrk_put(xt_value object, const char *key, xt_value value) {
  xt_object_set(object, xt_wrk_str(key), value);
}

static xt_value xt_wrk_fetch(xt_value object, const char *key) {
  return xt_object_get_cstr(object, key);
}

/* -- delivery ------------------------------------------------------------- */

static xt_value xt_wrk_port_drain(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)argc;
  (void)argv;
  xt_value port = xt_closure_env(env, 0);
  xt_wrk_put(port, "__xt_scheduled", XT_FALSE);
  if (xt_truthy(xt_wrk_fetch(port, "__xt_closed"))) return XT_UNDEFINED;
  xt_value queue = xt_wrk_fetch(port, "__xt_queue");
  if (!XT_IS_ARRAY(queue) || xt_array_size(queue) == 0) return XT_UNDEFINED;
  xt_wrk_put(port, "__xt_queue", xt_array_new(0, NULL));
  int32_t count = xt_array_size(queue);
  xt_value *items = xt_array_items(queue);
  for (int32_t i = 0; i < count; i++) xt_wrk_emit1(port, "message", items[i]);
  return XT_UNDEFINED;
}

static void xt_wrk_port_schedule(xt_value port) {
  if (xt_truthy(xt_wrk_fetch(port, "__xt_scheduled"))) return;
  xt_wrk_put(port, "__xt_scheduled", XT_TRUE);
  xt_value environment[1] = {port};
  xt_value arguments[2] = {xt_closure_new((void *)xt_wrk_port_drain, 1, environment),
                           xt_number(0)};
  xt_set_timeout(2, arguments);
}

static xt_value xt_wrk_port_post(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  if (xt_truthy(xt_wrk_fetch(self, "__xt_closed"))) return XT_UNDEFINED;
  xt_value peer = xt_wrk_fetch(self, "__xt_peer");
  if (!XT_IS_OBJECT(peer)) return XT_UNDEFINED;
  xt_value value = xt_arg_at(argc, argv, 0);
  xt_value clone = xt_structured_clone(1, &value);
  xt_value queue = xt_wrk_fetch(peer, "__xt_queue");
  if (!XT_IS_ARRAY(queue)) {
    queue = xt_array_new(0, NULL);
    xt_wrk_put(peer, "__xt_queue", queue);
  }
  xt_array_push(queue, clone);
  xt_wrk_port_schedule(peer);
  return XT_UNDEFINED;
}

static xt_value xt_wrk_port_close(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_put(self, "__xt_closed", XT_TRUE);
  xt_value peer = xt_wrk_fetch(self, "__xt_peer");
  if (XT_IS_OBJECT(peer)) xt_wrk_put(peer, "__xt_closed", XT_TRUE);
  xt_wrk_put(self, "__xt_peer", XT_UNDEFINED);
  xt_wrk_put(self, "__xt_queue", xt_array_new(0, NULL));
  return XT_UNDEFINED;
}

static xt_value xt_wrk_port_start(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_wrk_port_schedule(self);
  return XT_UNDEFINED;
}

/* -- object shape --------------------------------------------------------- */

static xt_value xt_wrk_port_new(void) {
  xt_value port = xt_object_new();
  xt_wrk_put(port, "__xt_port", XT_TRUE);
  xt_wrk_put(port, "__xt_queue", xt_array_new(0, NULL));
  xt_wrk_put(port, "__xt_scheduled", XT_FALSE);
  xt_wrk_install_emitter(port);
  xt_wrk_install_dom_events(port);
  xt_object_set(port, xt_wrk_str("postMessage"), xt_closure_new((void *)xt_wrk_port_post, 0, NULL));
  xt_object_set(port, xt_wrk_str("close"), xt_closure_new((void *)xt_wrk_port_close, 0, NULL));
  xt_object_set(port, xt_wrk_str("start"), xt_closure_new((void *)xt_wrk_port_start, 0, NULL));
  xt_object_set(port, xt_wrk_str("ref"), xt_closure_new((void *)xt_wrk_port_start, 0, NULL));
  xt_object_set(port, xt_wrk_str("unref"), xt_closure_new((void *)xt_wrk_port_start, 0, NULL));
  return port;
}

xt_value xt_wrk_new_channel(void) {
  xt_value port1 = xt_wrk_port_new();
  xt_value port2 = xt_wrk_port_new();
  xt_wrk_put(port1, "__xt_peer", port2);
  xt_wrk_put(port2, "__xt_peer", port1);
  xt_value channel = xt_object_new();
  xt_object_set(channel, xt_wrk_str("port1"), port1);
  xt_object_set(channel, xt_wrk_str("port2"), port2);
  return channel;
}

/* -- exported constructors ------------------------------------------------ */

static xt_value xt_wrk_channel_ctor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return xt_wrk_new_channel();
}

static xt_value xt_wrk_port_ctor(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("TypeError: MessagePort is not constructible"));
  return XT_UNDEFINED;
}

static xt_value xt_webworker_channel_value = XT_UNDEFINED;
static xt_value xt_webworker_port_value = XT_UNDEFINED;

xt_value xt_webworker_message_channel(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_FUNCTION(xt_webworker_channel_value)) {
    xt_webworker_channel_value = xt_closure_new((void *)xt_wrk_channel_ctor, 0, NULL);
    xt_gc_add_root(&xt_webworker_channel_value);
  }
  return xt_webworker_channel_value;
}

xt_value xt_webworker_message_port(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  if (!XT_IS_FUNCTION(xt_webworker_port_value)) {
    xt_webworker_port_value = xt_closure_new((void *)xt_wrk_port_ctor, 0, NULL);
    xt_gc_add_root(&xt_webworker_port_value);
  }
  return xt_webworker_port_value;
}

xt_value xt_webworker_message_channel_new(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_wrk_new_channel();
}

xt_value xt_webworker_message_port_new(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("TypeError: MessagePort is not constructible"));
  return XT_UNDEFINED;
}

xt_value xt_wt_message_channel(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return xt_wrk_new_channel();
}

xt_value xt_wt_message_port(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  xt_throw(xt_error_from_message("TypeError: MessagePort is not constructible"));
  return XT_UNDEFINED;
}

/* `receiveMessageOnPort(port)` exists in Node; a local port already delivers on
 * a timer, so the synchronous read returns `undefined` rather than pretending
 * the queue is empty forever. */
xt_value xt_wt_receive_message_on_port(int32_t argc, xt_value *argv) {
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}
